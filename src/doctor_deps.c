
#include "doctor_deps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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
    if (!row) {
        db_res_free(res);
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->record_id = db_row_int(res, row, 0);
    snprintf(out->patient_id, sizeof(out->patient_id), "%s", db_row_str(res, row, 1));
    snprintf(out->doctor_id,  sizeof(out->doctor_id),  "%s", db_row_str(res, row, 2));
    out->visit_id = db_row_int(res, row, 3);
    snprintf(out->allergy_history, sizeof(out->allergy_history), "%s", db_row_str(res, row, 4));
    snprintf(out->past_history,    sizeof(out->past_history),    "%s", db_row_str(res, row, 5));
    snprintf(out->family_history,  sizeof(out->family_history),  "%s", db_row_str(res, row, 6));
    snprintf(out->update_time,     sizeof(out->update_time),     "%s", db_row_str(res, row, 7));

    db_res_free(res);
    return 1;
}

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
    out->record_id = db_row_int(res, row, 0);
    snprintf(out->patient_id, sizeof(out->patient_id), "%s", db_row_str(res, row, 1));
    snprintf(out->doctor_id,  sizeof(out->doctor_id),  "%s", db_row_str(res, row, 2));
    out->visit_id = db_row_int(res, row, 3);
    snprintf(out->allergy_history, sizeof(out->allergy_history), "%s", db_row_str(res, row, 4));
    snprintf(out->past_history,    sizeof(out->past_history),    "%s", db_row_str(res, row, 5));
    snprintf(out->family_history,  sizeof(out->family_history),  "%s", db_row_str(res, row, 6));
    snprintf(out->update_time,     sizeof(out->update_time),     "%s", db_row_str(res, row, 7));

    db_res_free(res);
    return 1;
}
