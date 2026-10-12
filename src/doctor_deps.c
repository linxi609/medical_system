/* ==========================================================================
 * doctor_deps.c —— 医生模块的补充查询 实现
 * --------------------------------------------------------------------------
 * 负责组：医生组
 * 作者：陈翔宇
 * 创建日期：2026-10-13
 * 对应头文件：inc/doctor_deps.h
 *
 * 【历史】本文件最初还"顶替"实现了 M03/M06 的 5 个函数
 *   （appointment_query_by_doctor_today、appointment_list_free、
 *    medicine_query_all、medicine_query_by_id、medicine_list_free），
 *   和 appointment.c / medicine.c 里的空壳重复定义，导致整个项目链接失败。
 *   2026-10-13 经维护者把这 5 个实现搬回各自文件，本文件删掉了重复部分。
 *
 * 【现在只剩】两条 M07 的补充查询：医生手里只有 visit_id / record_id 时
 *   要把病历取出来，medical_record.c 的接口没有这个查询，属于医生组自加，
 *   所以留在这个独立文件里，不占别的模块的头文件。
 * ========================================================================== */

#include "doctor_deps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * 本文件用到的小工具
 * -------------------------------------------------------------------------- */

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
