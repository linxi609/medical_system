/* ==========================================================================
 * doctor_deps.c —— 医生模块依赖的跨组函数 实现
 * --------------------------------------------------------------------------
 * 负责组：医生组
 * 作者：陈翔宇
 * 创建日期：2026-10-13
 * 对应头文件：inc/doctor_deps.h（那里说明了为什么要有这个文件）
 *
 * 本文件只实现医生工作台真正要用到的函数：
 *   - appointment_query_by_doctor_today  查当日待就诊（M03 的函数，患者组负责）
 *   - medicine_query_all / _by_id        药品查询（M06 的函数，管理员组负责）
 *   - medical_record_find_by_visit/_by_id 病历补充查询（M07 的补充）
 *
 * 不改 appointment.c / medicine.c 一行。等两组的实现合回各自文件后，
 * 删掉本文件和 doctor_deps.h、并把 Makefile 里的过滤去掉即可。
 * ========================================================================== */

#include "doctor_deps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEPS_SQL_BUF 2048

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

/* ==========================================================================
 * M03：查某医生当天待就诊的预约
 * --------------------------------------------------------------------------
 * appoint_date 是 DATE，直接和 CURDATE() 比；
 * time_slot 用 ORDER BY 排一下，上午排在下午前面，方便医生按顺序叫号。
 * ========================================================================== */
int appointment_query_by_doctor_today(DB_HANDLE* db, const char* doctor_id,
                                      Appointment** out_list, int* out_count) {
    char buf[DEPS_SQL_BUF];
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

/* ==========================================================================
 * M03：释放预约数组
 * --------------------------------------------------------------------------
 * 必须和上面的 appointment_query_by_doctor_today 配对提供：
 * Makefile 把 appointment.c 排除出编译后，两边的函数都要有人出，
 * 否则 menu_doctor 里调 appointment_list_free 会链接不上。
 * ========================================================================== */
void appointment_list_free(Appointment* list) {
    free(list);
}

/* ==========================================================================
 * M06：查全部药品
 * --------------------------------------------------------------------------
 * 按药品编号排序，列表展示稳定，医生翻库存时好找。
 * 库存不足的判断不在这里做，由调用方按 stock < 50 标注
 * （教程 1.4 M06：列表展示时库存低于 50 的行标注"库存不足"）。
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

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

/* ==========================================================================
 * M06：按编号查单个药品（开方时逐条校验用）
 * ========================================================================== */
int medicine_query_by_id(DB_HANDLE* db, const char* med_id, Medicine* out) {
    char buf[512];
    char esc[128];
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || !out) return -4;

    if (!escape_str(db, med_id, esc, sizeof(esc))) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT med_id, med_name, spec, unit, price, stock, manufacturer "
             "FROM t_medicine WHERE med_id = %s", esc);

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
 * M06：释放药品数组
 * ========================================================================== */
void medicine_list_free(Medicine* list) {
    free(list);
}

/* ==========================================================================
 * M07 补充：按就诊 ID 查病历
 * --------------------------------------------------------------------------
 * 医生接诊完写病历时，手里只有 visit_id，需要先找回这条病历才能改。
 * 一个就诊对应一条病历，所以取最新的一条即可。
 * ========================================================================== */
int medical_record_find_by_visit(DB_HANDLE* db, int visit_id, MedicalRecord* out) {
    char buf[512];
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!out || visit_id <= 0) return 0;

    snprintf(buf, sizeof(buf),
             "SELECT record_id, patient_id, doctor_id, visit_id, "
             "allergy_history, past_history, family_history, "
             "DATE_FORMAT(update_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_medical_record WHERE visit_id = %d "
             "ORDER BY record_id DESC LIMIT 1", visit_id);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {                       /* 这次就诊还没写过病历 */
        db_res_free(res);
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->record_id = col_int(res, row, 0);
    snprintf(out->patient_id, sizeof(out->patient_id), "%s", col_str(res, row, 1));
    snprintf(out->doctor_id,  sizeof(out->doctor_id),  "%s", col_str(res, row, 2));
    out->visit_id = col_int(res, row, 3);
    snprintf(out->allergy_history, sizeof(out->allergy_history), "%s", col_str(res, row, 4));
    snprintf(out->past_history,    sizeof(out->past_history),    "%s", col_str(res, row, 5));
    snprintf(out->family_history,  sizeof(out->family_history),  "%s", col_str(res, row, 6));
    snprintf(out->update_time,     sizeof(out->update_time),     "%s", col_str(res, row, 7));

    db_res_free(res);
    return 1;
}

/* ==========================================================================
 * M07 补充：按病历 ID 查单条病历
 * --------------------------------------------------------------------------
 * 医生在"书写病历 - 修改已有病历"里输入病历 ID，先查出来看当前内容，
 * 确认是本人的再改。
 * ========================================================================== */
int medical_record_find_by_id(DB_HANDLE* db, int record_id, MedicalRecord* out) {
    char buf[512];
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!out || record_id <= 0) return 0;

    snprintf(buf, sizeof(buf),
             "SELECT record_id, patient_id, doctor_id, visit_id, "
             "allergy_history, past_history, family_history, "
             "DATE_FORMAT(update_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_medical_record WHERE record_id = %d", record_id);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->record_id = col_int(res, row, 0);
    snprintf(out->patient_id, sizeof(out->patient_id), "%s", col_str(res, row, 1));
    snprintf(out->doctor_id,  sizeof(out->doctor_id),  "%s", col_str(res, row, 2));
    out->visit_id = col_int(res, row, 3);
    snprintf(out->allergy_history, sizeof(out->allergy_history), "%s", col_str(res, row, 4));
    snprintf(out->past_history,    sizeof(out->past_history),    "%s", col_str(res, row, 5));
    snprintf(out->family_history,  sizeof(out->family_history),  "%s", col_str(res, row, 6));
    snprintf(out->update_time,     sizeof(out->update_time),     "%s", col_str(res, row, 7));

    db_res_free(res);
    return 1;
}
