/* ==========================================================================
 * visit.c —— M04 接诊与就诊记录 实现
 * --------------------------------------------------------------------------
 * 负责组：医生组
 * 作者：陈翔宇
 * 创建日期：2026-10-13
 * 对应头文件：inc/visit.h
 * 对应表：t_visit_record（本模块）、t_appointment（接诊时同步改状态）
 *
 * 实现说明：
 *   visit_add 按 visit.h 顶部的事务要求，把"写 t_visit_record"和"把对应
 *   预约的 status 置 1"放在同一个事务里；任何一步不成立就 ROLLBACK，
 *   不会留下"有就诊记录但预约还挂在待就诊"或者反过来的半截状态。
 *
 *   所有外部输入在拼进 SQL 之前都过一遍 escape_str() 转义，
 *   避免一个单引号把 SQL 拼坏。
 * ========================================================================== */

#include "visit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 拼 SQL 用的缓冲区：最长的一条是 INSERT，字段转义后约 2KB，取 4KB 留足余量 */
#define VISIT_SQL_BUF 4096
/* 单个文本字段的转义缓冲区：主诉/现病史在结构体里各 500 字节，转义后最多 1000 */
#define VISIT_ESC_BUF 1100

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
static int visit_load(DB_HANDLE* db, const char* where,
                      VisitRecord** out_list, int* out_count);

/* ==========================================================================
 * 接诊：写入就诊记录，并把对应预约状态置为已就诊
 * 返回：0 成功；-1 未连接；-2 SQL 失败（已回滚）；-4 预约不存在；
 *       -6 预约不属于该医生 / 已被接诊 / 已被取消
 * ========================================================================== */
int visit_add(DB_HANDLE* db, VisitRecord* v) {
    char  buf[VISIT_SQL_BUF];
    char  esc_pid[128];
    char  esc_did[128];
    char  esc_chief[VISIT_ESC_BUF];
    char  esc_present[VISIT_ESC_BUF];
    char  esc_bp[128];
    char* lit_pid;
    char* lit_did;
    char* lit_chief;
    char* lit_present;
    char* lit_bp;
    DB_RES* res;
    DB_ROW* row;
    int   affected;
    int   ret = -2;                 /* 默认按 SQL 失败处理，出错分支里再改具体码 */

    /* ---------- 入参校验 ---------- */
    if (!db || !db->is_connected) return -1;    /* -1 数据库连接失败 */
    if (!v) return -6;
    if (v->appoint_id <= 0) return -4;          /* 没带预约 ID，按预约不存在处理 */

    /* ---------- 事务开始 ---------- */
    int t = begin_tx(db);
    if (t != 0) return t;                       /* -1 未连接 / -2 BEGIN 失败 */

    /* ---------- 校验预约：必须存在、属于本人、且还是"待就诊" ----------
     * 加 FOR UPDATE 锁住这一行，防止同一条预约被重复接诊。 */
    snprintf(buf, sizeof(buf),
             "SELECT status, doctor_id FROM t_appointment "
             "WHERE appoint_id = %d FOR UPDATE", v->appoint_id);

    res = db_query(db, buf);
    if (!res) goto fail;                        /* 查询失败 */

    row = db_fetch_row(res);
    if (!row) {                                 /* 查不到 => 预约不存在 */
        db_res_free(res);
        ret = -4;
        goto fail;
    }
    if (col_int(res, row, 0) != 0) {            /* 列 0 = status，不是 0 就不能接诊 */
        db_res_free(res);
        ret = -6;
        goto fail;
    }
    if (strcmp(col_str(res, row, 1), v->doctor_id) != 0) {   /* 列 1 = doctor_id */
        db_res_free(res);
        ret = -6;
        goto fail;
    }
    db_res_free(res);

    /* ---------- 转义各文本字段 ---------- */
    lit_pid     = escape_str(db, v->patient_id,      esc_pid,     sizeof(esc_pid));
    lit_did     = escape_str(db, v->doctor_id,       esc_did,     sizeof(esc_did));
    lit_chief   = escape_str(db, v->chief_complaint, esc_chief,   sizeof(esc_chief));
    lit_present = escape_str(db, v->present_illness, esc_present, sizeof(esc_present));
    lit_bp      = escape_str(db, v->blood_pressure,  esc_bp,      sizeof(esc_bp));
    if (!lit_pid || !lit_did || !lit_chief || !lit_present || !lit_bp) {
        ret = -2;
        goto fail;
    }

    /* ---------- 写就诊记录 ----------
     * visit_time 不在这里赋值，交给建表语句里的 DEFAULT CURRENT_TIMESTAMP 填。 */
    snprintf(buf, sizeof(buf),
             "INSERT INTO t_visit_record "
             "(patient_id, doctor_id, appoint_id, chief_complaint, present_illness, "
             " temp, blood_pressure, heart_rate) "
             "VALUES (%s, %s, %d, %s, %s, %.1f, %s, %d)",
             lit_pid, lit_did, v->appoint_id, lit_chief, lit_present,
             v->temp, lit_bp, v->heart_rate);

    if (db_execute(db, buf) < 0) goto fail;

    /* 取回自增主键回写到 v->visit_id */
    v->visit_id = (int)mysql_insert_id(db->mysql);
    if (v->visit_id <= 0) goto fail;            /* 拿不到主键，说明这条没进去 */

    /* 顺手把数据库认定的就诊时间读回来，供调用方直接展示 */
    snprintf(buf, sizeof(buf),
             "SELECT DATE_FORMAT(visit_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_visit_record WHERE visit_id = %d", v->visit_id);
    res = db_query(db, buf);
    if (res) {
        row = db_fetch_row(res);
        if (row) {
            snprintf(v->visit_time, sizeof(v->visit_time), "%s", col_str(res, row, 0));
        }
        db_res_free(res);
    }

    /* ---------- 同一事务内把预约置为已就诊 ----------
     * 条件里带上 status = 0，影响行数为 0 说明这行已经被改过了，同样要回滚。 */
    snprintf(buf, sizeof(buf),
             "UPDATE t_appointment SET status = 1 "
             "WHERE appoint_id = %d AND status = 0", v->appoint_id);

    affected = db_execute(db, buf);
    if (affected < 0) goto fail;                /* SQL 执行失败 */
    if (affected == 0) { ret = -6; goto fail; } /* 没改到行：状态已被别人改动 */

    /* ---------- 提交 ---------- */
    db_execute(db, "COMMIT");
    return 0;

fail:
    db_execute(db, "ROLLBACK");
    return ret;
}

/* ==========================================================================
 * 结果集 -> VisitRecord 数组（三个查询函数共用）
 * 参数：where  形如 "WHERE patient_id = 'P20260001'" 的条件串，空串表示不加条件
 * 返回：0 成功（可能 out_count 为 0）；-1 未连接；-2 SQL 失败
 * ========================================================================== */
static int visit_load(DB_HANDLE* db, const char* where,
                      VisitRecord** out_list, int* out_count) {
    char buf[VISIT_SQL_BUF];
    DB_RES* res;
    DB_ROW* row;
    VisitRecord* arr = NULL;
    int n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT visit_id, patient_id, doctor_id, appoint_id, "
             "DATE_FORMAT(visit_time, '%%Y-%%m-%%d %%H:%%i:%%s'), "
             "chief_complaint, present_illness, temp, blood_pressure, heart_rate "
             "FROM t_visit_record %s ORDER BY visit_id DESC",
             where ? where : "");

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (VisitRecord*)malloc(sizeof(VisitRecord) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            VisitRecord* p;

            /* 双保险：数组是按 res->row_cnt 分配的，理论上取不到这么多行；
             * 万一结果集比报出来的行数还多，宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            p = &arr[n];
            memset(p, 0, sizeof(*p));
            p->visit_id   = col_int(res, row, 0);
            snprintf(p->patient_id, sizeof(p->patient_id), "%s", col_str(res, row, 1));
            snprintf(p->doctor_id,  sizeof(p->doctor_id),  "%s", col_str(res, row, 2));
            p->appoint_id = col_int(res, row, 3);
            snprintf(p->visit_time, sizeof(p->visit_time), "%s", col_str(res, row, 4));
            snprintf(p->chief_complaint, sizeof(p->chief_complaint), "%s", col_str(res, row, 5));
            snprintf(p->present_illness, sizeof(p->present_illness), "%s", col_str(res, row, 6));
            p->temp       = col_double(res, row, 7);
            snprintf(p->blood_pressure, sizeof(p->blood_pressure), "%s", col_str(res, row, 8));
            p->heart_rate = col_int(res, row, 9);
            n++;
        }
        db_res_free(res);
    } else {
        db_res_free(res);
    }

    /* 一行都没有时返回 NULL，让调用方按"没有记录"处理（visit_list_free 收 NULL 也安全） */
    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

/* ==========================================================================
 * 查询某患者的全部就诊记录
 * ========================================================================== */
int visit_query_by_patient(DB_HANDLE* db, const char* patient_id,
                           VisitRecord** out_list, int* out_count) {
    char where[160];
    char esc[128];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -2;

    if (!escape_str(db, patient_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE patient_id = %s", esc);
    return visit_load(db, where, out_list, out_count);
}

/* ==========================================================================
 * 查询某医生经手的全部就诊记录
 * ========================================================================== */
int visit_query_by_doctor(DB_HANDLE* db, const char* doctor_id,
                          VisitRecord** out_list, int* out_count) {
    char where[160];
    char esc[128];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!doctor_id || !doctor_id[0]) return -2;

    if (!escape_str(db, doctor_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE doctor_id = %s", esc);
    return visit_load(db, where, out_list, out_count);
}

/* ==========================================================================
 * 查询全部就诊记录（管理员用）
 * ========================================================================== */
int visit_query_all(DB_HANDLE* db, VisitRecord** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return visit_load(db, "", out_list, out_count);
}

/* ==========================================================================
 * 释放 visit_query_* 返回的数组
 * ========================================================================== */
void visit_list_free(VisitRecord* list) {
    free(list);
}
