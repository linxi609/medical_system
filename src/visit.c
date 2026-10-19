
#include "visit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VISIT_SQL_BUF 4096
#define VISIT_ESC_BUF 1100


static int visit_load(DB_HANDLE* db, const char* where,
                      VisitRecord** out_list, int* out_count);

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
    int   ret = -2;

    if (!db || !db->is_connected) return -1;
    if (!v) return -6;
    if (v->appoint_id <= 0) return -4;

    int t = db_begin_tx(db);
    if (t != 0) return t;

    snprintf(buf, sizeof(buf),
             "SELECT status, doctor_id FROM t_appointment "
             "WHERE appoint_id = %d FOR UPDATE", v->appoint_id);

    res = db_query(db, buf);
    if (!res) goto fail;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        ret = -4;
        goto fail;
    }
    if (db_row_int(res, row, 0) != 0) {
        db_res_free(res);
        ret = -6;
        goto fail;
    }
    if (strcmp(db_row_str(res, row, 1), v->doctor_id) != 0) {
        db_res_free(res);
        ret = -6;
        goto fail;
    }
    db_res_free(res);

    lit_pid     = db_escape_literal(db, v->patient_id,      esc_pid,     sizeof(esc_pid));
    lit_did     = db_escape_literal(db, v->doctor_id,       esc_did,     sizeof(esc_did));
    lit_chief   = db_escape_literal(db, v->chief_complaint, esc_chief,   sizeof(esc_chief));
    lit_present = db_escape_literal(db, v->present_illness, esc_present, sizeof(esc_present));
    lit_bp      = db_escape_literal(db, v->blood_pressure,  esc_bp,      sizeof(esc_bp));
    if (!lit_pid || !lit_did || !lit_chief || !lit_present || !lit_bp) {
        ret = -2;
        goto fail;
    }

    snprintf(buf, sizeof(buf),
             "INSERT INTO t_visit_record "
             "(patient_id, doctor_id, appoint_id, chief_complaint, present_illness, "
             " temp, blood_pressure, heart_rate) "
             "VALUES (%s, %s, %d, %s, %s, %.1f, %s, %d)",
             lit_pid, lit_did, v->appoint_id, lit_chief, lit_present,
             v->temp, lit_bp, v->heart_rate);

    if (db_execute(db, buf) < 0) goto fail;

    v->visit_id = (int)mysql_insert_id(db->mysql);
    if (v->visit_id <= 0) goto fail;

    snprintf(buf, sizeof(buf),
             "SELECT DATE_FORMAT(visit_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_visit_record WHERE visit_id = %d", v->visit_id);
    res = db_query(db, buf);
    if (res) {
        row = db_fetch_row(res);
        if (row) {
            snprintf(v->visit_time, sizeof(v->visit_time), "%s", db_row_str(res, row, 0));
        }
        db_res_free(res);
    }

    snprintf(buf, sizeof(buf),
             "UPDATE t_appointment SET status = 1 "
             "WHERE appoint_id = %d AND status = 0", v->appoint_id);

    affected = db_execute(db, buf);
    if (affected < 0) goto fail;
    if (affected == 0) { ret = -6; goto fail; }

    db_execute(db, "COMMIT");
    return 0;

fail:
    db_execute(db, "ROLLBACK");
    return ret;
}

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

            if (n >= res->row_cnt) break;

            p = &arr[n];
            memset(p, 0, sizeof(*p));
            p->visit_id   = db_row_int(res, row, 0);
            snprintf(p->patient_id, sizeof(p->patient_id), "%s", db_row_str(res, row, 1));
            snprintf(p->doctor_id,  sizeof(p->doctor_id),  "%s", db_row_str(res, row, 2));
            p->appoint_id = db_row_int(res, row, 3);
            snprintf(p->visit_time, sizeof(p->visit_time), "%s", db_row_str(res, row, 4));
            snprintf(p->chief_complaint, sizeof(p->chief_complaint), "%s", db_row_str(res, row, 5));
            snprintf(p->present_illness, sizeof(p->present_illness), "%s", db_row_str(res, row, 6));
            p->temp       = db_row_double(res, row, 7);
            snprintf(p->blood_pressure, sizeof(p->blood_pressure), "%s", db_row_str(res, row, 8));
            p->heart_rate = db_row_int(res, row, 9);
            n++;
        }
        db_res_free(res);
    } else {
        db_res_free(res);
    }

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

int visit_query_by_patient(DB_HANDLE* db, const char* patient_id,
                           VisitRecord** out_list, int* out_count) {
    char where[160];
    char esc[128];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -2;

    if (!db_escape_literal(db, patient_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE patient_id = %s", esc);
    return visit_load(db, where, out_list, out_count);
}

int visit_query_by_doctor(DB_HANDLE* db, const char* doctor_id,
                          VisitRecord** out_list, int* out_count) {
    char where[160];
    char esc[128];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!doctor_id || !doctor_id[0]) return -2;

    if (!db_escape_literal(db, doctor_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE doctor_id = %s", esc);
    return visit_load(db, where, out_list, out_count);
}

int visit_query_all(DB_HANDLE* db, VisitRecord** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return visit_load(db, "", out_list, out_count);
}

void visit_list_free(VisitRecord* list) {
    free(list);
}
