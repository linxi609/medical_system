
#include "prescription.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRESC_SQL_BUF 4096
#define PRESC_ESC_BUF 512
#define PRESC_MAX_ITEMS 64


static int presc_fetch_visit(DB_HANDLE* db, int visit_id,
                             char* out_patient, size_t patient_size,
                             char* out_doctor, size_t doctor_size);
static int presc_fetch_medicine(DB_HANDLE* db, const char* med_id,
                                double* out_price, int* out_stock);
static int presc_load(DB_HANDLE* db, const char* where,
                      Prescription** out_list, int* out_count);

int prescription_add(DB_HANDLE* db, Prescription* p,
                     PrescDetail* items, int item_count) {
    char   buf[PRESC_SQL_BUF];
    char   esc_diag[PRESC_ESC_BUF];
    char   esc_pid[128];
    char   esc_did[128];
    char   esc_med[128];
    char   esc_dos[512];
    char*  lit_diag;
    char*  lit_pid;
    char*  lit_did;
    char*  lit_med;
    char*  lit_dos;
    char   visit_patient[20];
    char   visit_doctor[10];
    double price;
    int    stock;
    int    i;
    int    visit_ok;
    int    med_ok;
    int    ret = -2;

    if (!db || !db->is_connected) return -1;
    if (!p || !items || item_count <= 0) return -2;
    if (p->visit_id <= 0) return -4;
    if (item_count > PRESC_MAX_ITEMS) return -2;

    int t = db_begin_tx(db);
    if (t != 0) return t;

    visit_ok = presc_fetch_visit(db, p->visit_id,
                                 visit_patient, sizeof(visit_patient),
                                 visit_doctor,  sizeof(visit_doctor));
    if (visit_ok < 0) goto fail;
    if (visit_ok == 0) { ret = -4; goto fail; }
    if (strcmp(visit_doctor, p->doctor_id) != 0) { ret = -6; goto fail; }

    snprintf(p->patient_id, sizeof(p->patient_id), "%s", visit_patient);

    lit_diag = db_escape_literal(db, p->diagnosis,  esc_diag, sizeof(esc_diag));
    lit_pid  = db_escape_literal(db, p->patient_id, esc_pid,  sizeof(esc_pid));
    lit_did  = db_escape_literal(db, p->doctor_id,  esc_did,  sizeof(esc_did));
    if (!lit_diag || !lit_pid || !lit_did) { ret = -2; goto fail; }

    snprintf(buf, sizeof(buf),
             "INSERT INTO t_prescription (visit_id, patient_id, doctor_id, diagnosis, status) "
             "VALUES (%d, %s, %s, %s, 1)",
             p->visit_id, lit_pid, lit_did, lit_diag);

    if (db_execute(db, buf) < 0) goto fail;

    p->presc_id = (int)mysql_insert_id(db->mysql);
    if (p->presc_id <= 0) goto fail;
    p->status = 1;

    for (i = 0; i < item_count; i++) {
        if (items[i].quantity <= 0) { ret = -5; goto fail; }

        med_ok = presc_fetch_medicine(db, items[i].med_id, &price, &stock);
        if (med_ok < 0) goto fail;
        if (med_ok == 0) { ret = -5; goto fail; }

        if (stock < items[i].quantity) { ret = -5; goto fail; }

        items[i].unit_price = price;
        items[i].subtotal   = (double)items[i].quantity * price;

        lit_med = db_escape_literal(db, items[i].med_id, esc_med, sizeof(esc_med));
        lit_dos = db_escape_literal(db, items[i].dosage, esc_dos, sizeof(esc_dos));
        if (!lit_med || !lit_dos) { ret = -2; goto fail; }

        snprintf(buf, sizeof(buf),
                 "INSERT INTO t_presc_detail "
                 "(presc_id, med_id, quantity, dosage, unit_price, subtotal) "
                 "VALUES (%d, %s, %d, %s, %.2f, %.2f)",
                 p->presc_id, lit_med, items[i].quantity, lit_dos,
                 items[i].unit_price, items[i].subtotal);

        if (db_execute(db, buf) < 0) goto fail;
        items[i].detail_id = (int)mysql_insert_id(db->mysql);
        items[i].presc_id  = p->presc_id;

        snprintf(buf, sizeof(buf),
                 "UPDATE t_medicine SET stock = stock - %d "
                 "WHERE med_id = %s AND stock >= %d",
                 items[i].quantity, lit_med, items[i].quantity);

        if (db_execute(db, buf) < 0) goto fail;

        if (mysql_affected_rows(db->mysql) == 0) { ret = -5; goto fail; }
    }

    db_execute(db, "COMMIT");
    return 0;

fail:
    db_execute(db, "ROLLBACK");
    return ret;
}

int prescription_void(DB_HANDLE* db, int presc_id, const char* doctor_id) {
    char   buf[512];
    char   esc[128];
    DB_RES* res;
    DB_ROW* row;
    int    affected;

    if (!db || !db->is_connected) return -1;
    if (presc_id <= 0) return -4;
    if (!doctor_id || !doctor_id[0]) return -6;

    snprintf(buf, sizeof(buf),
             "SELECT doctor_id, status FROM t_prescription WHERE presc_id = %d", presc_id);
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
    if (db_row_int(res, row, 1) != 1) {
        db_res_free(res);
        return -6;
    }
    db_res_free(res);

    snprintf(buf, sizeof(buf),
             "UPDATE t_prescription SET status = 0 "
             "WHERE presc_id = %d AND doctor_id = %s AND status = 1",
             presc_id, db_escape_literal(db, doctor_id, esc, sizeof(esc)));

    affected = db_execute(db, buf);
    if (affected < 0) return -2;
    if (affected == 0) return -6;

    return 0;
}

int prescription_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                  Prescription** out_list, int* out_count) {
    char where[160];
    char esc[128];

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -2;

    if (!db_escape_literal(db, patient_id, esc, sizeof(esc))) return -2;
    snprintf(where, sizeof(where), "WHERE patient_id = %s", esc);
    return presc_load(db, where, out_list, out_count);
}

int prescription_query_all(DB_HANDLE* db, Prescription** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return presc_load(db, "", out_list, out_count);
}

int prescription_query_by_id(DB_HANDLE* db, int presc_id,
                             Prescription* out,
                             PrescDetail** out_items, int* out_item_count) {
    char   buf[512];
    DB_RES* res;
    DB_ROW* row;
    PrescDetail* arr = NULL;
    int    n = 0;

    if (out_items)      *out_items      = NULL;
    if (out_item_count) *out_item_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out || presc_id <= 0) return -4;

    memset(out, 0, sizeof(*out));

    snprintf(buf, sizeof(buf),
             "SELECT presc_id, visit_id, patient_id, doctor_id, diagnosis, status, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_prescription WHERE presc_id = %d", presc_id);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }

    out->presc_id = db_row_int(res, row, 0);
    out->visit_id = db_row_int(res, row, 1);
    snprintf(out->patient_id,  sizeof(out->patient_id),  "%s", db_row_str(res, row, 2));
    snprintf(out->doctor_id,   sizeof(out->doctor_id),   "%s", db_row_str(res, row, 3));
    snprintf(out->diagnosis,   sizeof(out->diagnosis),   "%s", db_row_str(res, row, 4));
    out->status = db_row_int(res, row, 5);
    snprintf(out->create_time, sizeof(out->create_time), "%s", db_row_str(res, row, 6));
    db_res_free(res);

    snprintf(buf, sizeof(buf),
             "SELECT detail_id, presc_id, med_id, quantity, dosage, unit_price, subtotal "
             "FROM t_presc_detail WHERE presc_id = %d ORDER BY detail_id", presc_id);

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (PrescDetail*)malloc(sizeof(PrescDetail) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            PrescDetail* d;

            if (n >= res->row_cnt) break;

            d = &arr[n];
            memset(d, 0, sizeof(*d));
            d->detail_id  = db_row_int(res, row, 0);
            d->presc_id   = db_row_int(res, row, 1);
            snprintf(d->med_id, sizeof(d->med_id), "%s", db_row_str(res, row, 2));
            d->quantity   = db_row_int(res, row, 3);
            snprintf(d->dosage, sizeof(d->dosage), "%s", db_row_str(res, row, 4));
            d->unit_price = db_row_double(res, row, 5);
            d->subtotal   = db_row_double(res, row, 6);
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    if (out_items)      *out_items      = arr;
    if (out_item_count) *out_item_count = n;
    return 0;
}

void prescription_list_free(Prescription* list) {
    free(list);
}

void prescription_detail_free(PrescDetail* items) {
    free(items);
}


static int presc_fetch_visit(DB_HANDLE* db, int visit_id,
                             char* out_patient, size_t patient_size,
                             char* out_doctor, size_t doctor_size) {
    char   buf[256];
    DB_RES* res;
    DB_ROW* row;

    snprintf(buf, sizeof(buf),
             "SELECT patient_id, doctor_id FROM t_visit_record WHERE visit_id = %d",
             visit_id);

    res = db_query(db, buf);
    if (!res) return -1;

    row = db_fetch_row(res);
    if (!row) { db_res_free(res); return 0; }

    snprintf(out_patient, patient_size, "%s", db_row_str(res, row, 0));
    snprintf(out_doctor,  doctor_size,  "%s", db_row_str(res, row, 1));
    db_res_free(res);
    return 1;
}

static int presc_fetch_medicine(DB_HANDLE* db, const char* med_id,
                                double* out_price, int* out_stock) {
    char   buf[512];
    char   esc[128];
    DB_RES* res;
    DB_ROW* row;

    if (!med_id || !med_id[0]) return 0;
    if (!db_escape_literal(db, med_id, esc, sizeof(esc))) return 0;

    snprintf(buf, sizeof(buf),
             "SELECT price, stock FROM t_medicine WHERE med_id = %s FOR UPDATE", esc);

    res = db_query(db, buf);
    if (!res) return -1;

    row = db_fetch_row(res);
    if (!row) { db_res_free(res); return 0; }

    if (out_price) *out_price = db_row_double(res, row, 0);
    if (out_stock) *out_stock = db_row_int(res, row, 1);
    db_res_free(res);
    return 1;
}

static int presc_load(DB_HANDLE* db, const char* where,
                      Prescription** out_list, int* out_count) {
    char   buf[PRESC_SQL_BUF];
    DB_RES* res;
    DB_ROW* row;
    Prescription* arr = NULL;
    int    n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT presc_id, visit_id, patient_id, doctor_id, diagnosis, status, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_prescription %s ORDER BY presc_id DESC",
             where ? where : "");

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Prescription*)malloc(sizeof(Prescription) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            Prescription* p;

            if (n >= res->row_cnt) break;

            p = &arr[n];
            memset(p, 0, sizeof(*p));
            p->presc_id = db_row_int(res, row, 0);
            p->visit_id = db_row_int(res, row, 1);
            snprintf(p->patient_id,  sizeof(p->patient_id),  "%s", db_row_str(res, row, 2));
            snprintf(p->doctor_id,   sizeof(p->doctor_id),   "%s", db_row_str(res, row, 3));
            snprintf(p->diagnosis,   sizeof(p->diagnosis),   "%s", db_row_str(res, row, 4));
            p->status = db_row_int(res, row, 5);
            snprintf(p->create_time, sizeof(p->create_time), "%s", db_row_str(res, row, 6));
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}
