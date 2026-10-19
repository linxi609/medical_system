
#include "appointment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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

            if (n >= res->row_cnt) break;

            a = &arr[n];
            memset(a, 0, sizeof(*a));
            a->appoint_id = db_row_int(res, row, 0);
            snprintf(a->patient_id,   sizeof(a->patient_id),   "%s", db_row_str(res, row, 1));
            snprintf(a->doctor_id,    sizeof(a->doctor_id),    "%s", db_row_str(res, row, 2));
            snprintf(a->appoint_date, sizeof(a->appoint_date), "%s", db_row_str(res, row, 3));
            snprintf(a->time_slot,    sizeof(a->time_slot),    "%s", db_row_str(res, row, 4));
            snprintf(a->symptom,      sizeof(a->symptom),      "%s", db_row_str(res, row, 5));
            a->status = db_row_int(res, row, 6);
            snprintf(a->create_time,  sizeof(a->create_time),  "%s", db_row_str(res, row, 7));
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

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

    if (!db_escape_literal(db, a->patient_id,   esc_pid,     sizeof(esc_pid)))     return -2;
    if (!db_escape_literal(db, a->doctor_id,    esc_did,     sizeof(esc_did)))     return -2;
    if (!db_escape_literal(db, a->appoint_date, esc_date,    sizeof(esc_date)))    return -2;
    if (!db_escape_literal(db, a->time_slot,    esc_slot,    sizeof(esc_slot)))    return -2;
    if (!db_escape_literal(db, a->symptom,      esc_symptom, sizeof(esc_symptom))) return -2;

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

    snprintf(buf, sizeof(buf),
             "INSERT INTO t_appointment "
             "(patient_id, doctor_id, appoint_date, time_slot, symptom, status) "
             "VALUES (%s, %s, %s, %s, %s, 0)",
             esc_pid, esc_did, esc_date, esc_slot, esc_symptom);

    if (db_execute(db, buf) < 0) return -2;

    a->appoint_id = (int)mysql_insert_id(db->mysql);
    if (a->appoint_id <= 0) return -2;
    a->status = 0;

    snprintf(buf, sizeof(buf),
             "SELECT DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_appointment WHERE appoint_id = %d", a->appoint_id);
    res = db_query(db, buf);
    if (res) {
        DB_ROW* row = db_fetch_row(res);
        if (row) {
            snprintf(a->create_time, sizeof(a->create_time), "%s", db_row_str(res, row, 0));
        }
        db_res_free(res);
    }

    return 0;
}

int appointment_cancel(DB_HANDLE* db, int appoint_id, const char* patient_id) {
    char buf[512];
    char esc[64];
    DB_RES* res;
    DB_ROW* row;
    int affected;

    if (!db || !db->is_connected) return -1;
    if (appoint_id <= 0) return -4;
    if (!patient_id || !patient_id[0]) return -6;
    if (!db_escape_literal(db, patient_id, esc, sizeof(esc))) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT patient_id, status FROM t_appointment WHERE appoint_id = %d",
             appoint_id);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }
    if (strcmp(db_row_str(res, row, 0), patient_id) != 0) {
        db_res_free(res);
        return -6;
    }
    if (db_row_int(res, row, 1) != 0) {
        db_res_free(res);
        return -6;
    }
    db_res_free(res);

    snprintf(buf, sizeof(buf),
             "UPDATE t_appointment SET status = 2 "
             "WHERE appoint_id = %d AND patient_id = %s AND status = 0",
             appoint_id, esc);

    affected = db_execute(db, buf);
    if (affected < 0) return -2;
    if (affected == 0) return -6;
    return 0;
}

int appointment_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                 Appointment** out_list, int* out_count) {
    char where[160];
    char esc[64];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -2;

    if (!db_escape_literal(db, patient_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE patient_id = %s", esc);
    return appointment_load(db, where, out_list, out_count);
}

int appointment_query_all(DB_HANDLE* db, Appointment** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return appointment_load(db, "", out_list, out_count);
}

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
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;
    if (!doctor_id || !doctor_id[0]) return -2;

    if (!db_escape_literal(db, doctor_id, esc, sizeof(esc))) return -2;

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

            if (n >= res->row_cnt) break;

            a = &arr[n];
            memset(a, 0, sizeof(*a));
            a->appoint_id = db_row_int(res, row, 0);
            snprintf(a->patient_id,   sizeof(a->patient_id),   "%s", db_row_str(res, row, 1));
            snprintf(a->doctor_id,    sizeof(a->doctor_id),    "%s", db_row_str(res, row, 2));
            snprintf(a->appoint_date, sizeof(a->appoint_date), "%s", db_row_str(res, row, 3));
            snprintf(a->time_slot,    sizeof(a->time_slot),    "%s", db_row_str(res, row, 4));
            snprintf(a->symptom,      sizeof(a->symptom),      "%s", db_row_str(res, row, 5));
            a->status = db_row_int(res, row, 6);
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
