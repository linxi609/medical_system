/* ==========================================================================
 * medical_record.c —— M07 电子病历 实现
 * --------------------------------------------------------------------------
 * 负责组：医生组
 * 作者：陈翔宇
 * 创建日期：2026-10-13
 * 对应头文件：inc/medical_record.h
 * 对应表：t_medical_record
 *
 * 实现说明：
 *   1) 过敏史是必填项。确实没有过敏就填空字符串表示"无过敏"，
 *      所以这里不能把空串当成"没填"拒绝掉，只拦住 NULL 指针。
 *   2) medical_record_update 的两条业务规则：
 *        - 只能改本人书写的病历          -> 不是本人返回 -6
 *        - 距今不超过 24 小时才能改      -> 超时返回 -7
 *      24 小时的判断用数据库的 NOW() 来算，和写入时用的时钟保持一致。
 * ========================================================================== */

#include "medical_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MR_SQL_BUF 4096
/* 过敏史/既往史/家族史在结构体里各 200 字节，转义后最多 400，取 512 足够 */
#define MR_ESC_BUF 512
/* 24 小时 = 1440 分钟，用来判断病历是否已锁定 */
#define MR_LOCK_MINUTES 1440

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

/* --------------------------------------------------------------------------
 * 内部函数声明
 * -------------------------------------------------------------------------- */
static int mr_visit_exists(DB_HANDLE* db, int visit_id);
static int mr_load(DB_HANDLE* db, const char* where,
                   MedicalRecord** out_list, int* out_count);

/* ==========================================================================
 * 新增病历
 * 返回：0 成功；-1 未连接；-2 SQL 失败；-4 就诊记录不存在
 * ========================================================================== */
int medical_record_add(DB_HANDLE* db, MedicalRecord* r) {
    char   buf[MR_SQL_BUF];
    char   esc_allergy[MR_ESC_BUF];
    char   esc_past[MR_ESC_BUF];
    char   esc_family[MR_ESC_BUF];
    char   esc_pid[128];
    char   esc_did[128];
    char*  lit_allergy;
    char*  lit_past;
    char*  lit_family;
    char*  lit_pid;
    char*  lit_did;
    DB_RES* res;
    DB_ROW* row;
    int    visit_ok;

    /* ---------- 入参校验 ---------- */
    if (!db || !db->is_connected) return -1;      /* -1 数据库连接失败 */
    if (!r) return -4;
    /* 每次就诊对应一条病历，所以 visit_id 必须给 */
    if (r->visit_id <= 0) return -4;

    /* ---------- 校验就诊记录存在 ---------- */
    visit_ok = mr_visit_exists(db, r->visit_id);
    if (visit_ok < 0) return -2;                  /* SQL 失败 */
    if (visit_ok == 0) return -4;                 /* 就诊记录不存在 */

    /* ---------- 转义各文本字段 ----------
     * 过敏史允许空串（表示"无过敏"），escape_str 对 NULL 也按空串处理，
     * 所以这里只判转义结果放不放得下。 */
    lit_allergy = escape_str(db, r->allergy_history, esc_allergy, sizeof(esc_allergy));
    lit_past    = escape_str(db, r->past_history,    esc_past,    sizeof(esc_past));
    lit_family  = escape_str(db, r->family_history,  esc_family,  sizeof(esc_family));
    lit_pid     = escape_str(db, r->patient_id,      esc_pid,     sizeof(esc_pid));
    lit_did     = escape_str(db, r->doctor_id,       esc_did,     sizeof(esc_did));
    if (!lit_allergy || !lit_past || !lit_family || !lit_pid || !lit_did) return -2;

    /* ---------- 插入 ----------
     * update_time 不在这里赋值，交给建表语句里的 DEFAULT CURRENT_TIMESTAMP 填，
     * 这样和上面"24 小时内可改"的判断用的是同一个时钟。 */
    snprintf(buf, sizeof(buf),
             "INSERT INTO t_medical_record "
             "(patient_id, doctor_id, visit_id, allergy_history, past_history, family_history) "
             "VALUES (%s, %s, %d, %s, %s, %s)",
             lit_pid, lit_did, r->visit_id, lit_allergy, lit_past, lit_family);

    if (db_execute(db, buf) < 0) return -2;

    /* 取回自增主键回写到 r->record_id */
    r->record_id = (int)mysql_insert_id(db->mysql);
    if (r->record_id <= 0) return -2;

    /* 顺手把数据库认定的更新时间读回来，供调用方直接展示 */
    snprintf(buf, sizeof(buf),
             "SELECT DATE_FORMAT(update_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_medical_record WHERE record_id = %d", r->record_id);

    res = db_query(db, buf);
    if (res) {
        row = db_fetch_row(res);
        if (row) {
            snprintf(r->update_time, sizeof(r->update_time), "%s", col_str(res, row, 0));
        }
        db_res_free(res);
    }

    return 0;
}

/* ==========================================================================
 * 修改病历
 * 返回：0 成功；-1 未连接；-2 SQL 失败；-4 病历不存在；
 *       -6 不是本人书写的；-7 已超过 24 小时锁定
 * ========================================================================== */
int medical_record_update(DB_HANDLE* db, const MedicalRecord* r) {
    char   buf[MR_SQL_BUF];
    char   esc_allergy[MR_ESC_BUF];
    char   esc_past[MR_ESC_BUF];
    char   esc_family[MR_ESC_BUF];
    char   esc_did[128];
    char*  lit_allergy;
    char*  lit_past;
    char*  lit_family;
    char*  lit_did;
    DB_RES* res;
    DB_ROW* row;
    int    locked;
    int    affected;

    /* ---------- 入参校验 ---------- */
    if (!db || !db->is_connected) return -1;
    if (!r || r->record_id <= 0) return -4;
    /* doctor_id 是定长数组，地址不会是 NULL，只需要判空串 */
    if (r->doctor_id[0] == '\0') return -6;

    /* ---------- 先查这条病历：在不在、谁写的、有没有超 24 小时 ---------- */
    snprintf(buf, sizeof(buf),
             "SELECT doctor_id, (TIMESTAMPDIFF(MINUTE, update_time, NOW()) >= %d) "
             "FROM t_medical_record WHERE record_id = %d",
             MR_LOCK_MINUTES, r->record_id);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {                                                /* 病历不存在 */
        db_res_free(res);
        return -4;
    }
    if (strcmp(col_str(res, row, 0), r->doctor_id) != 0) {    /* 列 0 = doctor_id */
        db_res_free(res);
        return -6;                                             /* 不是本人书写的 */
    }
    locked = col_int(res, row, 1);                             /* 列 1 = 是否超 24 小时 */
    db_res_free(res);

    if (locked) return -7;                    /* 已锁定，让调用方提示"病历已锁定" */

    /* ---------- 转义并更新 ---------- */
    lit_allergy = escape_str(db, r->allergy_history, esc_allergy, sizeof(esc_allergy));
    lit_past    = escape_str(db, r->past_history,    esc_past,    sizeof(esc_past));
    lit_family  = escape_str(db, r->family_history,  esc_family,  sizeof(esc_family));
    lit_did     = escape_str(db, r->doctor_id,       esc_did,     sizeof(esc_did));
    if (!lit_allergy || !lit_past || !lit_family || !lit_did) return -2;

    /* 条件里再带一次 doctor_id 和未超时判断：查完到改之间被人抢先改动时，
     * 这里会改到 0 行，不会越过规则把超时的病历改掉。
     * update_time 同时刷新为当前时间，修改完重新开始计 24 小时。 */
    snprintf(buf, sizeof(buf),
             "UPDATE t_medical_record SET "
             "allergy_history = %s, past_history = %s, family_history = %s, "
             "update_time = NOW() "
             "WHERE record_id = %d AND doctor_id = %s "
             "AND TIMESTAMPDIFF(MINUTE, update_time, NOW()) < %d",
             lit_allergy, lit_past, lit_family, r->record_id, lit_did, MR_LOCK_MINUTES);

    affected = db_execute(db, buf);
    if (affected < 0) return -2;
    if (affected == 0) return -7;    /* 没改到行：这期间已被改成超时或换了医生 */

    return 0;
}

/* ==========================================================================
 * 查询某患者的全部病历
 * ========================================================================== */
int medical_record_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                    MedicalRecord** out_list, int* out_count) {
    char where[160];
    char esc[128];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -2;

    if (!escape_str(db, patient_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE patient_id = %s", esc);
    return mr_load(db, where, out_list, out_count);
}

/* ==========================================================================
 * 查询全部病历（管理员用）
 * ========================================================================== */
int medical_record_query_all(DB_HANDLE* db, MedicalRecord** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return mr_load(db, "", out_list, out_count);
}

/* ==========================================================================
 * 释放 medical_record_query_* 返回的数组
 * ========================================================================== */
void medical_record_list_free(MedicalRecord* list) {
    free(list);
}

/* ==========================================================================
 * 内部函数
 * ========================================================================== */

/* 就诊记录是否存在：1 存在，0 不存在，-1 SQL 失败 */
static int mr_visit_exists(DB_HANDLE* db, int visit_id) {
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

/* 结果集 -> MedicalRecord 数组（两个查询函数共用） */
static int mr_load(DB_HANDLE* db, const char* where,
                   MedicalRecord** out_list, int* out_count) {
    char   buf[MR_SQL_BUF];
    DB_RES* res;
    DB_ROW* row;
    MedicalRecord* arr = NULL;
    int    n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT record_id, patient_id, doctor_id, visit_id, "
             "allergy_history, past_history, family_history, "
             "DATE_FORMAT(update_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_medical_record %s ORDER BY record_id DESC",
             where ? where : "");

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (MedicalRecord*)malloc(sizeof(MedicalRecord) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            MedicalRecord* m;

            /* 双保险：数组按 res->row_cnt 分配，理论上取不到这么多行；
             * 万一结果集比报出来的行数还多，宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            m = &arr[n];
            memset(m, 0, sizeof(*m));
            m->record_id = col_int(res, row, 0);
            snprintf(m->patient_id, sizeof(m->patient_id), "%s", col_str(res, row, 1));
            snprintf(m->doctor_id,  sizeof(m->doctor_id),  "%s", col_str(res, row, 2));
            m->visit_id  = col_int(res, row, 3);
            snprintf(m->allergy_history, sizeof(m->allergy_history), "%s", col_str(res, row, 4));
            snprintf(m->past_history,    sizeof(m->past_history),    "%s", col_str(res, row, 5));
            snprintf(m->family_history,  sizeof(m->family_history),  "%s", col_str(res, row, 6));
            snprintf(m->update_time,     sizeof(m->update_time),     "%s", col_str(res, row, 7));
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
