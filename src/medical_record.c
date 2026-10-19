
#include "medical_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MR_SQL_BUF 4096
#define MR_ESC_BUF 512
#define MR_LOCK_MINUTES 1440


static int mr_visit_exists(DB_HANDLE* db, int visit_id);
static int mr_load(DB_HANDLE* db, const char* where,
                   MedicalRecord** out_list, int* out_count);

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

    if (!db || !db->is_connected) return -1;
    if (!r) return -4;
    if (r->visit_id <= 0) return -4;

    visit_ok = mr_visit_exists(db, r->visit_id);
    if (visit_ok < 0) return -2;
    if (visit_ok == 0) return -4;

    lit_allergy = db_escape_literal(db, r->allergy_history, esc_allergy, sizeof(esc_allergy));
    lit_past    = db_escape_literal(db, r->past_history,    esc_past,    sizeof(esc_past));
    lit_family  = db_escape_literal(db, r->family_history,  esc_family,  sizeof(esc_family));
    lit_pid     = db_escape_literal(db, r->patient_id,      esc_pid,     sizeof(esc_pid));
    lit_did     = db_escape_literal(db, r->doctor_id,       esc_did,     sizeof(esc_did));
    if (!lit_allergy || !lit_past || !lit_family || !lit_pid || !lit_did) return -2;

    snprintf(buf, sizeof(buf),
             "INSERT INTO t_medical_record "
             "(patient_id, doctor_id, visit_id, allergy_history, past_history, family_history) "
             "VALUES (%s, %s, %d, %s, %s, %s)",
             lit_pid, lit_did, r->visit_id, lit_allergy, lit_past, lit_family);

    if (db_execute(db, buf) < 0) return -2;

    r->record_id = (int)mysql_insert_id(db->mysql);
    if (r->record_id <= 0) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT DATE_FORMAT(update_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_medical_record WHERE record_id = %d", r->record_id);

    res = db_query(db, buf);
    if (res) {
        row = db_fetch_row(res);
        if (row) {
            snprintf(r->update_time, sizeof(r->update_time), "%s", db_row_str(res, row, 0));
        }
        db_res_free(res);
    }

    return 0;
}

static int mr_check_editable(DB_HANDLE* db, int record_id, const char* doctor_id) {
    char   buf[256];
    DB_RES* res;
    DB_ROW* row;
    int    ret;

    snprintf(buf, sizeof(buf),
             "SELECT doctor_id, (TIMESTAMPDIFF(MINUTE, update_time, NOW()) >= %d) "
             "FROM t_medical_record WHERE record_id = %d",
             MR_LOCK_MINUTES, record_id);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }
    if (strcmp(db_row_str(res, row, 0), doctor_id) != 0) {
        db_res_free(res);
        return -6;
    }
    ret = db_row_int(res, row, 1) ? -7 : 0;
    db_res_free(res);
    return ret;
}

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
    int    ret;
    int    affected;

    if (!db || !db->is_connected) return -1;
    if (!r || r->record_id <= 0) return -4;
    if (r->doctor_id[0] == '\0') return -6;

    ret = mr_check_editable(db, r->record_id, r->doctor_id);
    if (ret != 0) return ret;

    lit_allergy = db_escape_literal(db, r->allergy_history, esc_allergy, sizeof(esc_allergy));
    lit_past    = db_escape_literal(db, r->past_history,    esc_past,    sizeof(esc_past));
    lit_family  = db_escape_literal(db, r->family_history,  esc_family,  sizeof(esc_family));
    lit_did     = db_escape_literal(db, r->doctor_id,       esc_did,     sizeof(esc_did));
    if (!lit_allergy || !lit_past || !lit_family || !lit_did) return -2;

    snprintf(buf, sizeof(buf),
             "UPDATE t_medical_record SET "
             "allergy_history = %s, past_history = %s, family_history = %s, "
             "update_time = NOW() "
             "WHERE record_id = %d AND doctor_id = %s "
             "AND TIMESTAMPDIFF(MINUTE, update_time, NOW()) < %d",
             lit_allergy, lit_past, lit_family, r->record_id, lit_did, MR_LOCK_MINUTES);

    affected = db_execute(db, buf);
    if (affected < 0) return -2;

    if (affected == 0) return mr_check_editable(db, r->record_id, r->doctor_id);

    return 0;
}

int medical_record_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                    MedicalRecord** out_list, int* out_count) {
    char where[160];
    char esc[128];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -2;

    if (!db_escape_literal(db, patient_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE patient_id = %s", esc);
    return mr_load(db, where, out_list, out_count);
}

int medical_record_query_all(DB_HANDLE* db, MedicalRecord** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return mr_load(db, "", out_list, out_count);
}

void medical_record_list_free(MedicalRecord* list) {
    free(list);
}


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

            if (n >= res->row_cnt) break;

            m = &arr[n];
            memset(m, 0, sizeof(*m));
            m->record_id = db_row_int(res, row, 0);
            snprintf(m->patient_id, sizeof(m->patient_id), "%s", db_row_str(res, row, 1));
            snprintf(m->doctor_id,  sizeof(m->doctor_id),  "%s", db_row_str(res, row, 2));
            m->visit_id  = db_row_int(res, row, 3);
            snprintf(m->allergy_history, sizeof(m->allergy_history), "%s", db_row_str(res, row, 4));
            snprintf(m->past_history,    sizeof(m->past_history),    "%s", db_row_str(res, row, 5));
            snprintf(m->family_history,  sizeof(m->family_history),  "%s", db_row_str(res, row, 6));
            snprintf(m->update_time,     sizeof(m->update_time),     "%s", db_row_str(res, row, 7));
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}
