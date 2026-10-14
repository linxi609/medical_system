/* ==========================================================================
 * appointment.c —— M03 挂号预约 实现
 * --------------------------------------------------------------------------
 * 负责组：患者组（挂号、取消）、医生组（当日列表、接诊入口）
 * 对应头文件：inc/appointment.h
 * 说明：函数声明和返回值含义见 appointment.h，本文件只填实现。
 *       status 含义：0 待就诊，1 已就诊，2 已取消，判断时直接写数字。
 *
 * 【当前进度】appointment_query_by_doctor_today 和 appointment_list_free
 *   先前写在 src/doctor_deps.c（医生组为了让工作台能跑临时顶替），2026-10-13
 *   经维护者搬回本文件、删掉了 doctor_deps.c 里的重复实现，项目才能链接。
 *   患者组写 appointment_add / appointment_cancel / appointment_query_by_patient /
 *   appointment_query_all 时，可一并复核这两个函数；行为要改的话，
 *   医生工作台依赖"只返回今天 + status=0 + 按 time_slot 排序"这三点。
 * ========================================================================== */

#include "appointment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ==========================================================================
 * 患者组负责的函数
 * --------------------------------------------------------------------------
 * appointment_add    挂号：先拦重复挂号（同患者/同天/同医生，且未取消的），
 *                    再插入 status=0 的新预约。
 * appointment_cancel 取消：先查这条预约在不在、是不是本人、是不是待就诊，
 *                    再 UPDATE status=2（条件里带 status=0，并发下也安全）。
 * 两个查询共用一个内部函数 appointment_load 装结果集。
 * ========================================================================== */

/* 结果集 -> Appointment 数组（三个查询函数共用；where 传空串表示不加条件） */
static int appointment_load(DB_HANDLE* db, const char* where,
                            Appointment** out_list, int* out_count) {
    char buf[2048];
    DB_RES* res;
    DB_ROW* row;
    Appointment* arr = NULL;
    int n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT appoint_id, patient_id, doctor_id, "
             "DATE_FORMAT(appoint_date, '%%Y-%%m-%%d'), time_slot, symptom, status, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_appointment %s ORDER BY appoint_id DESC",
             where ? where : "");

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Appointment*)malloc(sizeof(Appointment) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            Appointment* a;

            /* 双保险：数组按 res->row_cnt 分配，万一结果集比报出来的行数还多，
             * 宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            a = &arr[n];
            memset(a, 0, sizeof(*a));
            a->appoint_id = col_int(res, row, 0);
            snprintf(a->patient_id,   sizeof(a->patient_id),   "%s", col_str(res, row, 1));
            snprintf(a->doctor_id,    sizeof(a->doctor_id),    "%s", col_str(res, row, 2));
            snprintf(a->appoint_date, sizeof(a->appoint_date), "%s", col_str(res, row, 3));
            snprintf(a->time_slot,    sizeof(a->time_slot),    "%s", col_str(res, row, 4));
            snprintf(a->symptom,      sizeof(a->symptom),      "%s", col_str(res, row, 5));
            a->status = col_int(res, row, 6);
            snprintf(a->create_time,  sizeof(a->create_time),  "%s", col_str(res, row, 7));
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

/* 患者挂号
 * 返回：0 成功；-8 重复挂号；-2 SQL 执行失败；-1 连接失败 */
int appointment_add(DB_HANDLE* db, Appointment* a) {
    char buf[2048];
    char esc_pid[64];
    char esc_did[64];
    char esc_date[32];
    char esc_slot[32];
    char esc_symptom[448];
    DB_RES* res;

    if (!db || !db->is_connected) return -1;
    if (!a) return -2;
    if (!a->patient_id[0] || !a->doctor_id[0] || !a->appoint_date[0]) return -2;

    if (!escape_str(db, a->patient_id,   esc_pid,     sizeof(esc_pid)))     return -2;
    if (!escape_str(db, a->doctor_id,    esc_did,     sizeof(esc_did)))     return -2;
    if (!escape_str(db, a->appoint_date, esc_date,    sizeof(esc_date)))    return -2;
    if (!escape_str(db, a->time_slot,    esc_slot,    sizeof(esc_slot)))    return -2;
    if (!escape_str(db, a->symptom,      esc_symptom, sizeof(esc_symptom))) return -2;

    /* ---------- 重复挂号校验 ----------
     * 同一患者、同一天、同一医生，只要还有未取消的预约（status 0 或 1），
     * 就算重复。已取消（status=2）的不拦，可以重新挂。 */
    snprintf(buf, sizeof(buf),
             "SELECT appoint_id FROM t_appointment "
             "WHERE patient_id = %s AND doctor_id = %s "
             "AND appoint_date = %s AND status != 2 LIMIT 1",
             esc_pid, esc_did, esc_date);

    res = db_query(db, buf);
    if (!res) return -2;
    {
        int dup = (db_fetch_row(res) != NULL);
        db_res_free(res);
        if (dup) return -8;
    }

    /* ---------- 插入（status 固定 0，create_time 走 DEFAULT） ---------- */
    snprintf(buf, sizeof(buf),
             "INSERT INTO t_appointment "
             "(patient_id, doctor_id, appoint_date, time_slot, symptom, status) "
             "VALUES (%s, %s, %s, %s, %s, 0)",
             esc_pid, esc_did, esc_date, esc_slot, esc_symptom);

    if (db_execute(db, buf) < 0) return -2;

    a->appoint_id = (int)mysql_insert_id(db->mysql);
    if (a->appoint_id <= 0) return -2;      /* 拿不到自增主键，说明没插进去 */
    a->status = 0;

    /* 把数据库认定的挂号时间读回来，供调用方展示 */
    snprintf(buf, sizeof(buf),
             "SELECT DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_appointment WHERE appoint_id = %d", a->appoint_id);
    res = db_query(db, buf);
    if (res) {
        DB_ROW* row = db_fetch_row(res);
        if (row) {
            snprintf(a->create_time, sizeof(a->create_time), "%s", col_str(res, row, 0));
        }
        db_res_free(res);
    }

    return 0;
}

/* 取消预约
 * 返回：0 成功；-4 预约不存在；-6 不是本人的预约或状态不允许取消；
 *       -2 SQL 执行失败；-1 连接失败 */
int appointment_cancel(DB_HANDLE* db, int appoint_id, const char* patient_id) {
    char buf[512];
    char esc[64];
    DB_RES* res;
    DB_ROW* row;
    int affected;

    if (!db || !db->is_connected) return -1;
    if (appoint_id <= 0) return -4;
    if (!patient_id || !patient_id[0]) return -6;
    if (!escape_str(db, patient_id, esc, sizeof(esc))) return -2;

    /* ---------- 先查这条预约：在不在、谁的、什么状态 ----------
     * 先把 -4 和 -6 区分开，再动手改。 */
    snprintf(buf, sizeof(buf),
             "SELECT patient_id, status FROM t_appointment WHERE appoint_id = %d",
             appoint_id);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {                                    /* 预约不存在 */
        db_res_free(res);
        return -4;
    }
    if (strcmp(col_str(res, row, 0), patient_id) != 0) {  /* 列 0 = patient_id */
        db_res_free(res);
        return -6;                                 /* 不是本人的预约 */
    }
    if (col_int(res, row, 1) != 0) {               /* 列 1 = status */
        db_res_free(res);
        return -6;                                 /* 已就诊或已取消，不能再取消 */
    }
    db_res_free(res);

    /* ---------- 置为已取消 ----------
     * 条件里再带一次 status=0：查完到改之间被医生抢先接诊时，这里影响 0 行。 */
    snprintf(buf, sizeof(buf),
             "UPDATE t_appointment SET status = 2 "
             "WHERE appoint_id = %d AND patient_id = %s AND status = 0",
             appoint_id, esc);

    affected = db_execute(db, buf);
    if (affected < 0) return -2;
    if (affected == 0) return -6;      /* 状态已被改动，不再是可取消的待就诊 */
    return 0;
}

/* 查询某患者的全部预约 */
int appointment_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                 Appointment** out_list, int* out_count) {
    char where[160];
    char esc[64];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -2;

    if (!escape_str(db, patient_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE patient_id = %s", esc);
    return appointment_load(db, where, out_list, out_count);
}

/* 查询全部预约（管理员用） */
int appointment_query_all(DB_HANDLE* db, Appointment** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return appointment_load(db, "", out_list, out_count);
}

/* ==========================================================================
 * 医生组先实现的查询（原 doctor_deps.c）
 * --------------------------------------------------------------------------
 * appoint_date 是 DATE，直接和 CURDATE() 比；
 * time_slot 用 ORDER BY 排一下，上午排在下午前面，方便医生按顺序叫号。
 * ========================================================================== */
int appointment_query_by_doctor_today(DB_HANDLE* db, const char* doctor_id,
                                      Appointment** out_list, int* out_count) {
    char buf[2048];
    char esc[128];
    DB_RES* res;
    DB_ROW* row;
    Appointment* arr = NULL;
    int n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;      /* -1 数据库连接失败 */
    if (!out_list || !out_count) return -2;
    if (!doctor_id || !doctor_id[0]) return -2;

    if (!escape_str(db, doctor_id, esc, sizeof(esc))) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT appoint_id, patient_id, doctor_id, "
             "DATE_FORMAT(appoint_date, '%%Y-%%m-%%d'), time_slot, symptom, status, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_appointment "
             "WHERE doctor_id = %s AND appoint_date = CURDATE() AND status = 0 "
             "ORDER BY time_slot, appoint_id", esc);

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Appointment*)malloc(sizeof(Appointment) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            Appointment* a;

            /* 双保险：数组按 res->row_cnt 分配，理论上取不到这么多行；
             * 万一结果集比报出来的行数还多，宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            a = &arr[n];
            memset(a, 0, sizeof(*a));
            a->appoint_id = col_int(res, row, 0);
            snprintf(a->patient_id,   sizeof(a->patient_id),   "%s", col_str(res, row, 1));
            snprintf(a->doctor_id,    sizeof(a->doctor_id),    "%s", col_str(res, row, 2));
            snprintf(a->appoint_date, sizeof(a->appoint_date), "%s", col_str(res, row, 3));
            snprintf(a->time_slot,    sizeof(a->time_slot),    "%s", col_str(res, row, 4));
            snprintf(a->symptom,      sizeof(a->symptom),      "%s", col_str(res, row, 5));
            a->status = col_int(res, row, 6);
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

void appointment_list_free(Appointment* list) {
    free(list);
}
