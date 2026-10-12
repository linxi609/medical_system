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
 * 患者组负责的函数（还是空壳，等患者组填）
 * ========================================================================== */

int appointment_add(DB_HANDLE* db, Appointment* a) {
    (void)db; (void)a;
    return 0;  /* TODO: 校验同一患者同天同医生未重复，插入 t_appointment（status=0） */
}

int appointment_cancel(DB_HANDLE* db, int appoint_id, const char* patient_id) {
    (void)db; (void)appoint_id; (void)patient_id;
    return 0;  /* TODO: 校验预约属于该患者且 status=0，再 UPDATE status=2 */
}

int appointment_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                 Appointment** out_list, int* out_count) {
    (void)db; (void)patient_id;
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    return 0;  /* TODO: SELECT ... FROM t_appointment WHERE patient_id=? */
}

int appointment_query_all(DB_HANDLE* db, Appointment** out_list, int* out_count) {
    (void)db;
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    return 0;  /* TODO: SELECT 全部预约（管理员用） */
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
