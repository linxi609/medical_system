
#include "medicine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MED_SQL_BUF 1024
#define MED_ESC_BUF 128


static int med_exists(DB_HANDLE* db, const char* med_id) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    DB_RES* res;
    int found;

    lit = db_escape_literal(db, med_id, esc, sizeof(esc));
    if (!lit) return -1;

    snprintf(buf, sizeof(buf), "SELECT med_id FROM t_medicine WHERE med_id = %s", lit);
    res = db_query(db, buf);
    if (!res) return -1;

    found = (db_fetch_row(res) != NULL) ? 1 : 0;
    db_res_free(res);
    return found;
}

int medicine_add(DB_HANDLE* db, Medicine* m) {
    char buf[MED_SQL_BUF];
    char esc_id[MED_ESC_BUF];
    char esc_name[MED_ESC_BUF];
    char esc_spec[MED_ESC_BUF];
    char esc_unit[MED_ESC_BUF];
    char esc_man[MED_ESC_BUF];
    char* lit_id;
    char* lit_name;
    char* lit_spec;
    char* lit_unit;
    char* lit_man;
    DB_RES* res;
    DB_ROW* row;
    int exists;
    int idlen;
    int seq = 0;

    if (!db || !db->is_connected) return -1;
    if (!m) return -2;

    res = db_query(db,
                   "SELECT MAX(CAST(SUBSTRING(med_id, 2) AS UNSIGNED)) "
                   "FROM t_medicine WHERE med_id REGEXP '^M[0-9]+$'");
    if (!res) return -2;

    row = db_fetch_row(res);
    if (row && row->fields[0]) seq = atoi(row->fields[0]);
    db_res_free(res);
    seq++;
    idlen = snprintf(m->med_id, sizeof(m->med_id), "M%03d", seq);
    if (idlen < 0 || (size_t)idlen >= sizeof(m->med_id)) return -2;

    exists = med_exists(db, m->med_id);
    if (exists < 0) return -2;
    if (exists > 0) return -3;

    lit_id   = db_escape_literal(db, m->med_id,       esc_id,   sizeof(esc_id));
    lit_name = db_escape_literal(db, m->med_name,     esc_name, sizeof(esc_name));
    lit_spec = db_escape_literal(db, m->spec,         esc_spec, sizeof(esc_spec));
    lit_unit = db_escape_literal(db, m->unit,         esc_unit, sizeof(esc_unit));
    lit_man  = db_escape_literal(db, m->manufacturer, esc_man,  sizeof(esc_man));
    if (!lit_id || !lit_name || !lit_spec || !lit_unit || !lit_man) return -2;

    snprintf(buf, sizeof(buf),
             "INSERT INTO t_medicine "
             "(med_id, med_name, spec, unit, price, stock, manufacturer) "
             "VALUES (%s, %s, %s, %s, %.2f, %d, %s)",
             lit_id, lit_name, lit_spec, lit_unit, m->price, m->stock, lit_man);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

int medicine_stock_in(DB_HANDLE* db, const char* med_id, int quantity) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    int exists;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || quantity <= 0) return -2;

    lit = db_escape_literal(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    exists = med_exists(db, med_id);
    if (exists < 0) return -2;
    if (exists == 0) return -4;

    snprintf(buf, sizeof(buf),
             "UPDATE t_medicine SET stock = stock + %d WHERE med_id = %s",
             quantity, lit);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

int medicine_stock_out(DB_HANDLE* db, const char* med_id, int quantity) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    DB_RES* res;
    DB_ROW* row;
    int stock;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || quantity <= 0) return -2;

    lit = db_escape_literal(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    snprintf(buf, sizeof(buf), "SELECT stock FROM t_medicine WHERE med_id = %s", lit);
    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }
    stock = db_row_int(res, row, 0);
    db_res_free(res);

    if (stock < quantity) return -5;

    snprintf(buf, sizeof(buf),
             "UPDATE t_medicine SET stock = stock - %d WHERE med_id = %s",
             quantity, lit);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

int medicine_update_price(DB_HANDLE* db, const char* med_id, double new_price) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    int exists;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || new_price < 0) return -2;

    lit = db_escape_literal(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    exists = med_exists(db, med_id);
    if (exists < 0) return -2;
    if (exists == 0) return -4;

    snprintf(buf, sizeof(buf),
             "UPDATE t_medicine SET price = %.2f WHERE med_id = %s",
             new_price, lit);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

int medicine_delete(DB_HANDLE* db, const char* med_id) {
    char buf[256];
    char esc[MED_ESC_BUF];
    char* lit;
    DB_RES* res;
    int exists;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0]) return -4;

    lit = db_escape_literal(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    exists = med_exists(db, med_id);
    if (exists < 0) return -2;
    if (exists == 0) return -4;

    snprintf(buf, sizeof(buf),
             "SELECT detail_id FROM t_presc_detail WHERE med_id = %s LIMIT 1", lit);
    res = db_query(db, buf);
    if (!res) return -2;
    {
        int used = (db_fetch_row(res) != NULL);
        db_res_free(res);
        if (used) return -6;
    }

    snprintf(buf, sizeof(buf), "DELETE FROM t_medicine WHERE med_id = %s", lit);
    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

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

            if (n >= res->row_cnt) break;

            m = &arr[n];
            memset(m, 0, sizeof(*m));
            snprintf(m->med_id,       sizeof(m->med_id),       "%s", db_row_str(res, row, 0));
            snprintf(m->med_name,     sizeof(m->med_name),     "%s", db_row_str(res, row, 1));
            snprintf(m->spec,         sizeof(m->spec),         "%s", db_row_str(res, row, 2));
            snprintf(m->unit,         sizeof(m->unit),         "%s", db_row_str(res, row, 3));
            m->price = db_row_double(res, row, 4);
            m->stock = db_row_int(res, row, 5);
            snprintf(m->manufacturer, sizeof(m->manufacturer), "%s", db_row_str(res, row, 6));
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

int medicine_query_by_id(DB_HANDLE* db, const char* med_id, Medicine* out) {
    char buf[512];
    char esc[MED_ESC_BUF];
    char* lit;
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || !out) return -4;

    lit = db_escape_literal(db, med_id, esc, sizeof(esc));
    if (!lit) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT med_id, med_name, spec, unit, price, stock, manufacturer "
             "FROM t_medicine WHERE med_id = %s", lit);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->med_id,       sizeof(out->med_id),       "%s", db_row_str(res, row, 0));
    snprintf(out->med_name,     sizeof(out->med_name),     "%s", db_row_str(res, row, 1));
    snprintf(out->spec,         sizeof(out->spec),         "%s", db_row_str(res, row, 2));
    snprintf(out->unit,         sizeof(out->unit),         "%s", db_row_str(res, row, 3));
    out->price = db_row_double(res, row, 4);
    out->stock = db_row_int(res, row, 5);
    snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", db_row_str(res, row, 6));

    db_res_free(res);
    return 0;
}

void medicine_list_free(Medicine* list) {
    free(list);
}
