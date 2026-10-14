/* ==========================================================================
 * prescription.c —— M05 处方管理 实现
 * --------------------------------------------------------------------------
 * 负责组：医生组
 * 作者：陈翔宇
 * 创建日期：2026-10-13
 * 对应头文件：inc/prescription.h
 * 对应表：t_prescription（主表）、t_presc_detail（明细）、t_medicine（扣库存）
 *
 * 实现说明：
 *   1) prescription_add 按 prescription.h 的事务要求，把"写主表""写明细"
 *      "扣库存"三步放在同一个事务里。任一味药库存不足或药品不存在，
 *      整张处方都不生效，统一 ROLLBACK。
 *   2) unit_price 取开方当刻 t_medicine.price 的快照，之后药品调价不影响
 *      已有处方；subtotal = quantity × unit_price 由本函数算好入库。
 *   3) 同一张处方里若重复出现同一个药品编号，会叠加校验已占用的数量，
 *      避免"每行单独看都够、合起来超库存"。
 *   4) prescription_void 只把 status 置 0，不回滚库存——药已经发给患者了。
 * ========================================================================== */

#include "prescription.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRESC_SQL_BUF 4096
/* 诊断结果在结构体里 200 字节，转义后最多 400，取 512 足够 */
#define PRESC_ESC_BUF 512
/* 一次处方最多允许的明细条数（受下面栈上 used[] 的容量限制） */
#define PRESC_MAX_ITEMS 64

/* --------------------------------------------------------------------------
 * 本文件用到的小工具
 * -------------------------------------------------------------------------- */

/* 把字符串转义后拼成 SQL 字面量（带两边的单引号），写进 dst。
 * 直接拼字符串的话，用户输入里带个单引号就会把 SQL 拼坏，所以统一走一遍
 * mysql_real_escape_string。src 传 NULL 时按空字符串处理。
 * dst 装不下时返回 NULL，调用方跳过即可。 */
static char* escape_str(DB_HANDLE* db, const char* src, char* dst, size_t dstsz) {
    size_t n;
    if (!dst || dstsz < 3) return NULL;
    if (!src) src = "";
    n = mysql_real_escape_string(db->mysql, dst + 1, src, (unsigned long)strlen(src));
    if (n + 3 > dstsz) return NULL;
    dst[0] = '\'';
    dst[n + 1] = '\'';
    dst[n + 2] = '\0';
    return dst;
}

/* 取一行里第 idx 列，NULL 列或越界都返回空字符串 */
static const char* col_str(DB_RES* res, DB_ROW* row, int idx) {
    if (!row || !row->fields) return "";
    if (idx < 0 || idx >= row->col_cnt || idx >= res->col_cnt) return "";
    if (!row->fields[idx]) return "";
    return row->fields[idx];
}

/* 取一行里第 idx 列当整数用 */
static int col_int(DB_RES* res, DB_ROW* row, int idx) {
    const char* s = col_str(res, row, idx);
    return s[0] ? atoi(s) : 0;
}

/* 取一行里第 idx 列当小数用 */
static double col_double(DB_RES* res, DB_ROW* row, int idx) {
    const char* s = col_str(res, row, idx);
    return s[0] ? atof(s) : 0.0;
}

/* 开事务。0 成功，非 0 是给调用方直接返回的错误码 */
static int begin_tx(DB_HANDLE* db) {
    if (!db || !db->is_connected) return -1;
    if (db_execute(db, "BEGIN") < 0) return -2;
    return 0;
}

/* --------------------------------------------------------------------------
 * 内部函数声明
 * -------------------------------------------------------------------------- */
static int presc_visit_exists(DB_HANDLE* db, int visit_id);
static int presc_fetch_medicine(DB_HANDLE* db, const char* med_id,
                                double* out_price, int* out_stock);
static int presc_load(DB_HANDLE* db, const char* where,
                      Prescription** out_list, int* out_count);

/* ==========================================================================
 * 开处方
 * 返回：0 成功；-1 未连接；-2 SQL 失败（已回滚）；-4 就诊记录不存在；
 *       -5 库存不足（已回滚）
 * ========================================================================== */
int prescription_add(DB_HANDLE* db, Prescription* p,
                     PrescDetail* items, int item_count) {
    char   buf[PRESC_SQL_BUF];
    char   esc_diag[PRESC_ESC_BUF];
    char   esc_pid[128];
    char   esc_did[128];
    char   esc_med[128];
    char   esc_dos[512];
    char*  lit_diag;
    char*  lit_pid;
    char*  lit_did;
    char*  lit_med;
    char*  lit_dos;
    int    used[PRESC_MAX_ITEMS];   /* 第 i 条明细累计占用的数量（含前面重复的药品） */
    double price;
    int    stock;
    int    already;
    int    i;
    int    j;
    int    visit_ok;
    int    med_ok;
    int    ret = -2;                /* 默认按 SQL 失败处理，出错分支里再改具体码 */

    /* ---------- 入参校验 ---------- */
    if (!db || !db->is_connected) return -1;          /* -1 数据库连接失败 */
    if (!p || !items || item_count <= 0) return -2;
    if (p->visit_id <= 0) return -4;                  /* 没挂就诊记录，按不存在处理 */
    if (item_count > PRESC_MAX_ITEMS) return -2;      /* 超出 used[] 容量，拒绝 */

    /* ---------- 事务开始 ---------- */
    int t = begin_tx(db);
    if (t != 0) return t;                             /* -1 未连接 / -2 BEGIN 失败 */

    /* ---------- 校验就诊记录存在 ----------
     * 处方必须挂在自己出具的就诊记录上，否则会出现"给别人的就诊开处方"。 */
    visit_ok = presc_visit_exists(db, p->visit_id);
    if (visit_ok < 0) goto fail;                      /* SQL 失败 */
    if (visit_ok == 0) { ret = -4; goto fail; }       /* 就诊记录不存在 */

    /* ---------- 写处方主表 ---------- */
    lit_diag = escape_str(db, p->diagnosis,  esc_diag, sizeof(esc_diag));
    lit_pid  = escape_str(db, p->patient_id, esc_pid,  sizeof(esc_pid));
    lit_did  = escape_str(db, p->doctor_id,  esc_did,  sizeof(esc_did));
    if (!lit_diag || !lit_pid || !lit_did) { ret = -2; goto fail; }

    snprintf(buf, sizeof(buf),
             "INSERT INTO t_prescription (visit_id, patient_id, doctor_id, diagnosis, status) "
             "VALUES (%d, %s, %s, %s, 1)",
             p->visit_id, lit_pid, lit_did, lit_diag);

    if (db_execute(db, buf) < 0) goto fail;

    p->presc_id = (int)mysql_insert_id(db->mysql);
    if (p->presc_id <= 0) goto fail;                  /* 拿不到自增主键，说明没插进去 */
    p->status = 1;

    /* ---------- 逐条明细：查单价、校验库存、写明细、扣库存 ---------- */
    for (i = 0; i < item_count; i++) {
        if (items[i].quantity <= 0) { ret = -5; goto fail; }

        /* 这一行要多少，加上前面已经开过的同一种药，不能超过库存 */
        already = 0;
        for (j = 0; j < i; j++) {
            if (strcmp(items[j].med_id, items[i].med_id) == 0) already += items[j].quantity;
        }
        used[i] = already + items[i].quantity;

        /* 查药品当前单价和库存，FOR UPDATE 锁住这一行，防止并发开方超卖 */
        med_ok = presc_fetch_medicine(db, items[i].med_id, &price, &stock);
        if (med_ok < 0) goto fail;                    /* SQL 失败 */
        if (med_ok == 0) { ret = -5; goto fail; }     /* 药品不存在，开不出来 */

        if (stock < used[i]) { ret = -5; goto fail; } /* 库存不足，整张处方回滚 */

        /* 单价快照 + 小计，算好填回结构体，调用方可以直接展示金额 */
        items[i].unit_price = price;
        items[i].subtotal   = (double)items[i].quantity * price;

        /* med_id 和 dosage 各用一个转义缓冲区：
         * 两次转义如果共用同一块内存，后一次的结果会把前一次覆盖掉 */
        lit_med = escape_str(db, items[i].med_id, esc_med, sizeof(esc_med));
        lit_dos = escape_str(db, items[i].dosage, esc_dos, sizeof(esc_dos));
        if (!lit_med || !lit_dos) { ret = -2; goto fail; }

        snprintf(buf, sizeof(buf),
                 "INSERT INTO t_presc_detail "
                 "(presc_id, med_id, quantity, dosage, unit_price, subtotal) "
                 "VALUES (%d, %s, %d, %s, %.2f, %.2f)",
                 p->presc_id, lit_med, items[i].quantity, lit_dos,
                 items[i].unit_price, items[i].subtotal);

        if (db_execute(db, buf) < 0) goto fail;
        items[i].detail_id = (int)mysql_insert_id(db->mysql);
        items[i].presc_id  = p->presc_id;

        /* 扣减库存。条件里带上 stock >= used[i]，真正超卖时这里影响 0 行 */
        snprintf(buf, sizeof(buf),
                 "UPDATE t_medicine SET stock = stock - %d "
                 "WHERE med_id = %s AND stock >= %d",
                 items[i].quantity, lit_med, used[i]);

        if (db_execute(db, buf) < 0) goto fail;

        /* 库存扣减后肯定和原值不同，所以影响行数为 0 只可能是"没有这个药品" */
        if (mysql_affected_rows(db->mysql) == 0) { ret = -5; goto fail; }
    }

    /* ---------- 提交 ---------- */
    db_execute(db, "COMMIT");
    return 0;

fail:
    db_execute(db, "ROLLBACK");
    return ret;
}

/* ==========================================================================
 * 作废处方：把 status 置 0，不回滚库存
 * 返回：0 成功；-1 未连接；-2 SQL 失败；-4 处方不存在；
 *       -6 不是本人开的，或已是作废状态
 * ========================================================================== */
int prescription_void(DB_HANDLE* db, int presc_id, const char* doctor_id) {
    char   buf[512];
    char   esc[128];
    DB_RES* res;
    DB_ROW* row;
    int    affected;

    if (!db || !db->is_connected) return -1;
    if (presc_id <= 0) return -4;
    if (!doctor_id || !doctor_id[0]) return -6;

    /* 先看这张处方在不在、是谁开的、什么状态，才能把 -4 和 -6 区分开 */
    snprintf(buf, sizeof(buf),
             "SELECT doctor_id, status FROM t_prescription WHERE presc_id = %d", presc_id);
    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {                                              /* 处方不存在 */
        db_res_free(res);
        return -4;
    }
    if (strcmp(col_str(res, row, 0), doctor_id) != 0) {    /* 列 0 = doctor_id */
        db_res_free(res);
        return -6;                                           /* 不是本人开的 */
    }
    if (col_int(res, row, 1) != 1) {                         /* 列 1 = status */
        db_res_free(res);
        return -6;                                           /* 已经作废了 */
    }
    db_res_free(res);

    /* 条件里带上原状态：并发下被别人先作废时，这里影响 0 行 */
    snprintf(buf, sizeof(buf),
             "UPDATE t_prescription SET status = 0 "
             "WHERE presc_id = %d AND doctor_id = %s AND status = 1",
             presc_id, escape_str(db, doctor_id, esc, sizeof(esc)));

    affected = db_execute(db, buf);
    if (affected < 0) return -2;
    if (affected == 0) return -6;    /* 状态已被改动，不再是可作废的有效处方 */

    return 0;
}

/* ==========================================================================
 * 查询某患者的全部处方（不含明细，列表展示用）
 * ========================================================================== */
int prescription_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                  Prescription** out_list, int* out_count) {
    char where[160];
    char esc[128];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -2;

    if (!escape_str(db, patient_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE patient_id = %s", esc);
    return presc_load(db, where, out_list, out_count);
}

/* ==========================================================================
 * 查询全部处方（管理员用）
 * ========================================================================== */
int prescription_query_all(DB_HANDLE* db, Prescription** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return presc_load(db, "", out_list, out_count);
}

/* ==========================================================================
 * 按 ID 查询一张处方及其全部明细
 * 返回：0 成功；-1 未连接；-2 SQL 失败；-4 处方不存在
 * ========================================================================== */
int prescription_query_by_id(DB_HANDLE* db, int presc_id,
                             Prescription* out,
                             PrescDetail** out_items, int* out_item_count) {
    char   buf[512];
    DB_RES* res;
    DB_ROW* row;
    PrescDetail* arr = NULL;
    int    n = 0;

    if (out_items)      *out_items      = NULL;
    if (out_item_count) *out_item_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out || presc_id <= 0) return -4;

    memset(out, 0, sizeof(*out));

    /* ---------- 主表 ---------- */
    snprintf(buf, sizeof(buf),
             "SELECT presc_id, visit_id, patient_id, doctor_id, diagnosis, status, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_prescription WHERE presc_id = %d", presc_id);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {                                  /* 处方不存在 */
        db_res_free(res);
        return -4;
    }

    out->presc_id = col_int(res, row, 0);
    out->visit_id = col_int(res, row, 1);
    snprintf(out->patient_id,  sizeof(out->patient_id),  "%s", col_str(res, row, 2));
    snprintf(out->doctor_id,   sizeof(out->doctor_id),   "%s", col_str(res, row, 3));
    snprintf(out->diagnosis,   sizeof(out->diagnosis),   "%s", col_str(res, row, 4));
    out->status = col_int(res, row, 5);
    snprintf(out->create_time, sizeof(out->create_time), "%s", col_str(res, row, 6));
    db_res_free(res);

    /* ---------- 明细 ---------- */
    snprintf(buf, sizeof(buf),
             "SELECT detail_id, presc_id, med_id, quantity, dosage, unit_price, subtotal "
             "FROM t_presc_detail WHERE presc_id = %d ORDER BY detail_id", presc_id);

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (PrescDetail*)malloc(sizeof(PrescDetail) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            PrescDetail* d;

            /* 双保险：数组按 res->row_cnt 分配，理论上取不到这么多行；
             * 万一结果集比报出来的行数还多，宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            d = &arr[n];
            memset(d, 0, sizeof(*d));
            d->detail_id  = col_int(res, row, 0);
            d->presc_id   = col_int(res, row, 1);
            snprintf(d->med_id, sizeof(d->med_id), "%s", col_str(res, row, 2));
            d->quantity   = col_int(res, row, 3);
            snprintf(d->dosage, sizeof(d->dosage), "%s", col_str(res, row, 4));
            d->unit_price = col_double(res, row, 5);
            d->subtotal   = col_double(res, row, 6);
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    if (out_items)      *out_items      = arr;
    if (out_item_count) *out_item_count = n;
    return 0;
}

/* ==========================================================================
 * 释放
 * ========================================================================== */
void prescription_list_free(Prescription* list) {
    free(list);
}

void prescription_detail_free(PrescDetail* items) {
    free(items);
}

/* ==========================================================================
 * 内部函数
 * ========================================================================== */

/* 就诊记录是否存在：1 存在，0 不存在，-1 SQL 失败 */
static int presc_visit_exists(DB_HANDLE* db, int visit_id) {
    char   buf[256];
    DB_RES* res;
    DB_ROW* row;
    int    found;

    snprintf(buf, sizeof(buf),
             "SELECT visit_id FROM t_visit_record WHERE visit_id = %d", visit_id);

    res = db_query(db, buf);
    if (!res) return -1;

    row   = db_fetch_row(res);
    found = row ? 1 : 0;
    db_res_free(res);
    return found;
}

/* 查药品当前单价和库存（加行锁）：1 找到，0 没这个药品，-1 SQL 失败 */
static int presc_fetch_medicine(DB_HANDLE* db, const char* med_id,
                                double* out_price, int* out_stock) {
    char   buf[512];
    char   esc[128];
    DB_RES* res;
    DB_ROW* row;

    if (!med_id || !med_id[0]) return 0;
    if (!escape_str(db, med_id, esc, sizeof(esc))) return 0;

    snprintf(buf, sizeof(buf),
             "SELECT price, stock FROM t_medicine WHERE med_id = %s FOR UPDATE", esc);

    res = db_query(db, buf);
    if (!res) return -1;

    row = db_fetch_row(res);
    if (!row) { db_res_free(res); return 0; }

    if (out_price) *out_price = col_double(res, row, 0);
    if (out_stock) *out_stock = col_int(res, row, 1);
    db_res_free(res);
    return 1;
}

/* 处方主表 -> Prescription 数组（两个查询函数共用） */
static int presc_load(DB_HANDLE* db, const char* where,
                      Prescription** out_list, int* out_count) {
    char   buf[PRESC_SQL_BUF];
    DB_RES* res;
    DB_ROW* row;
    Prescription* arr = NULL;
    int    n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT presc_id, visit_id, patient_id, doctor_id, diagnosis, status, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_prescription %s ORDER BY presc_id DESC",
             where ? where : "");

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Prescription*)malloc(sizeof(Prescription) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            Prescription* p;

            /* 双保险：数组按 res->row_cnt 分配，理论上取不到这么多行；
             * 万一结果集比报出来的行数还多，宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            p = &arr[n];
            memset(p, 0, sizeof(*p));
            p->presc_id = col_int(res, row, 0);
            p->visit_id = col_int(res, row, 1);
            snprintf(p->patient_id,  sizeof(p->patient_id),  "%s", col_str(res, row, 2));
            snprintf(p->doctor_id,   sizeof(p->doctor_id),   "%s", col_str(res, row, 3));
            snprintf(p->diagnosis,   sizeof(p->diagnosis),   "%s", col_str(res, row, 4));
            p->status = col_int(res, row, 5);
            snprintf(p->create_time, sizeof(p->create_time), "%s", col_str(res, row, 6));
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}
