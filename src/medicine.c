/* ==========================================================================
 * medicine.c —— M06 药品库存管理 实现
 * --------------------------------------------------------------------------
 * 负责组：管理员组
 * 对应头文件：inc/medicine.h
 * 对应表：t_medicine
 *
 * 实现说明：
 *   1) 药品编号 med_id 是主键，新增前先查一次，重复返回 -3；
 *      入库 / 出库 / 调价前也都先查编号是否存在，不存在返回 -4。
 *   2) 出库前先查当前库存，出库数量大于库存时返回 -5，不扣减。
 *   3) 库存预警由调用方（菜单层）按 stock < 50 标注，这里不重复判断。
 *   4) 单条语句无需事务；开方扣库存走 prescription_add 自己的事务，
 *      不经过这里的 medicine_stock_out（见 medicine.h 注释）。
 * ========================================================================== */

#include "medicine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 主 SQL 缓冲区。INSERT 里最多拼 5 个文本字段（各最多 50 字节），1024 足够 */
#define MED_SQL_BUF 1024
/* 药品各文本字段最长 50 字节，转义后最多 2*50+3=103，取 128 足够 */
#define MED_ESC_BUF 128

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

/* --------------------------------------------------------------------------
 * 查某编号的药品是否存在：1 存在，0 不存在，-1 SQL 失败
 * -------------------------------------------------------------------------- */
static int med_exists(DB_HANDLE* db, const char* med_id) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    DB_RES* res;
    int found;

    lit = escape_str(db, med_id, esc, sizeof(esc));
    if (!lit) return -1;

    snprintf(buf, sizeof(buf), "SELECT med_id FROM t_medicine WHERE med_id = %s", lit);
    res = db_query(db, buf);
    if (!res) return -1;

    found = (db_fetch_row(res) != NULL) ? 1 : 0;
    db_res_free(res);
    return found;
}

/* ==========================================================================
 * 新增药品
 * 返回：0 成功；-3 药品编号已存在；-2 SQL 执行失败
 * ========================================================================== */
int medicine_add(DB_HANDLE* db, const Medicine* m) {
    char buf[MED_SQL_BUF];
    char esc_id[MED_ESC_BUF];
    char esc_name[MED_ESC_BUF];
    char esc_spec[MED_ESC_BUF];
    char esc_unit[MED_ESC_BUF];
    char esc_man[MED_ESC_BUF];
    char* lit_id;
    char* lit_name;
    char* lit_spec;
    char* lit_unit;
    char* lit_man;
    int exists;

    if (!db || !db->is_connected) return -1;      /* -1 数据库连接失败 */
    if (!m || !m->med_id[0]) return -2;           /* 编号必填 */

    /* 主键重复靠数据库唯一约束也能拦住，但先查一次能给调用方精确的 -3 */
    exists = med_exists(db, m->med_id);
    if (exists < 0) return -2;                    /* 查询失败 */
    if (exists > 0) return -3;                    /* 药品编号已存在 */

    lit_id   = escape_str(db, m->med_id,       esc_id,   sizeof(esc_id));
    lit_name = escape_str(db, m->med_name,     esc_name, sizeof(esc_name));
    lit_spec = escape_str(db, m->spec,         esc_spec, sizeof(esc_spec));
    lit_unit = escape_str(db, m->unit,         esc_unit, sizeof(esc_unit));
    lit_man  = escape_str(db, m->manufacturer, esc_man,  sizeof(esc_man));
    if (!lit_id || !lit_name || !lit_spec || !lit_unit || !lit_man) return -2;

    snprintf(buf, sizeof(buf),
             "INSERT INTO t_medicine "
             "(med_id, med_name, spec, unit, price, stock, manufacturer) "
             "VALUES (%s, %s, %s, %s, %.2f, %d, %s)",
             lit_id, lit_name, lit_spec, lit_unit, m->price, m->stock, lit_man);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

/* ==========================================================================
 * 入库：增加库存
 * 返回：0 成功；-4 药品不存在；-2 SQL 执行失败
 * ========================================================================== */
int medicine_stock_in(DB_HANDLE* db, const char* med_id, int quantity) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    int exists;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || quantity <= 0) return -2;

    lit = escape_str(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    exists = med_exists(db, med_id);
    if (exists < 0) return -2;
    if (exists == 0) return -4;                   /* 药品不存在 */

    snprintf(buf, sizeof(buf),
             "UPDATE t_medicine SET stock = stock + %d WHERE med_id = %s",
             quantity, lit);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

/* ==========================================================================
 * 出库：减少库存
 * 返回：0 成功；-4 药品不存在；-5 库存不足；-2 SQL 执行失败
 * ========================================================================== */
int medicine_stock_out(DB_HANDLE* db, const char* med_id, int quantity) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    DB_RES* res;
    DB_ROW* row;
    int stock;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || quantity <= 0) return -2;

    lit = escape_str(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    /* 先查当前库存，顺便确认药品存在 */
    snprintf(buf, sizeof(buf), "SELECT stock FROM t_medicine WHERE med_id = %s", lit);
    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;                                /* 药品不存在 */
    }
    stock = col_int(res, row, 0);
    db_res_free(res);

    if (stock < quantity) return -5;              /* 库存不足，拒绝出库 */

    snprintf(buf, sizeof(buf),
             "UPDATE t_medicine SET stock = stock - %d WHERE med_id = %s",
             quantity, lit);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

/* ==========================================================================
 * 调价：直接更新单价
 * 返回：0 成功；-4 药品不存在；-2 SQL 执行失败
 * ========================================================================== */
int medicine_update_price(DB_HANDLE* db, const char* med_id, double new_price) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    int exists;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || new_price < 0) return -2;

    lit = escape_str(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    exists = med_exists(db, med_id);
    if (exists < 0) return -2;
    if (exists == 0) return -4;                   /* 药品不存在 */

    snprintf(buf, sizeof(buf),
             "UPDATE t_medicine SET price = %.2f WHERE med_id = %s",
             new_price, lit);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

/* ==========================================================================
 * 查询全部药品（盘点用）
 * 返回：0 成功（可能 out_count 为 0）；-2 SQL 执行失败
 * ========================================================================== */
int medicine_query_all(DB_HANDLE* db, Medicine** out_list, int* out_count) {
    DB_RES* res;
    DB_ROW* row;
    Medicine* arr = NULL;
    int n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    res = db_query(db,
                   "SELECT med_id, med_name, spec, unit, price, stock, manufacturer "
                   "FROM t_medicine ORDER BY med_id");
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Medicine*)malloc(sizeof(Medicine) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            Medicine* m;

            /* 双保险：数组按 res->row_cnt 分配，理论上取不到这么多行；
             * 万一结果集比报出来的行数还多，宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            m = &arr[n];
            memset(m, 0, sizeof(*m));
            snprintf(m->med_id,       sizeof(m->med_id),       "%s", col_str(res, row, 0));
            snprintf(m->med_name,     sizeof(m->med_name),     "%s", col_str(res, row, 1));
            snprintf(m->spec,         sizeof(m->spec),         "%s", col_str(res, row, 2));
            snprintf(m->unit,         sizeof(m->unit),         "%s", col_str(res, row, 3));
            m->price = col_double(res, row, 4);
            m->stock = col_int(res, row, 5);
            snprintf(m->manufacturer, sizeof(m->manufacturer), "%s", col_str(res, row, 6));
            n++;
        }
    }
    db_res_free(res);

    /* 一行都没有时返回 NULL，让调用方按"没有记录"处理 */
    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

/* ==========================================================================
 * 按编号查询单个药品（医生开方前查库存用）
 * 返回：0 成功；-4 未找到；-2 SQL 执行失败
 * ========================================================================== */
int medicine_query_by_id(DB_HANDLE* db, const char* med_id, Medicine* out) {
    char buf[512];
    char esc[MED_ESC_BUF];
    char* lit;
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || !out) return -4;

    lit = escape_str(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT med_id, med_name, spec, unit, price, stock, manufacturer "
             "FROM t_medicine WHERE med_id = %s", lit);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {                       /* 没这个药品 */
        db_res_free(res);
        return -4;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->med_id,       sizeof(out->med_id),       "%s", col_str(res, row, 0));
    snprintf(out->med_name,     sizeof(out->med_name),     "%s", col_str(res, row, 1));
    snprintf(out->spec,         sizeof(out->spec),         "%s", col_str(res, row, 2));
    snprintf(out->unit,         sizeof(out->unit),         "%s", col_str(res, row, 3));
    out->price = col_double(res, row, 4);
    out->stock = col_int(res, row, 5);
    snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", col_str(res, row, 6));

    db_res_free(res);
    return 0;
}

/* ==========================================================================
 * 释放 medicine_query_all 返回的数组
 * ========================================================================== */
void medicine_list_free(Medicine* list) {
    free(list);
}
