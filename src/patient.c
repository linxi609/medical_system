
#include "patient.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAT_SQL_BUF 4096


static int patient_exists(DB_HANDLE* db, const char* patient_id);
static int patient_load(DB_HANDLE* db, const char* where,
                        Patient** out_list, int* out_count);

int patient_register(DB_HANDLE* db, Patient* p) {
    char    buf[PAT_SQL_BUF];
    char    esc_name[128];
    char    esc_gender[32];
    char    esc_card[64];
    char    esc_phone[64];
    char    esc_addr[448];
    char    esc_user[128];
    char    esc_pwd[224];
    DB_RES* res;
    DB_ROW* row;
    int     seq = 0;

    if (!db || !db->is_connected) return -1;
    if (!p) return -2;

    res = db_query(db,
                   "SELECT MAX(CAST(SUBSTRING(patient_id, 6) AS UNSIGNED)) "
                   "FROM t_patient "
                   "WHERE patient_id LIKE CONCAT('P', YEAR(NOW()), '%')");
    if (!res) return -2;

    row = db_fetch_row(res);
    if (row && row->fields[0]) seq = atoi(row->fields[0]);
    db_res_free(res);
    seq++;

    {
        char year[8] = "";
        res = db_query(db, "SELECT YEAR(NOW())");
        if (!res) return -2;
        row = db_fetch_row(res);
        if (row && row->fields[0]) snprintf(year, sizeof(year), "%s", row->fields[0]);
        db_res_free(res);
        if (!year[0]) return -2;
        snprintf(p->patient_id, sizeof(p->patient_id), "P%s%04d", year, seq);
    }

    snprintf(p->username, sizeof(p->username), "%s", p->patient_id);

    if (!db_escape_literal(db, p->name,     esc_name,   sizeof(esc_name)))   return -2;
    if (!db_escape_literal(db, p->gender,   esc_gender, sizeof(esc_gender))) return -2;
    if (!db_escape_literal(db, p->id_card,  esc_card,   sizeof(esc_card)))   return -2;
    if (!db_escape_literal(db, p->phone,    esc_phone,  sizeof(esc_phone)))  return -2;
    if (!db_escape_literal(db, p->address,  esc_addr,   sizeof(esc_addr)))   return -2;
    if (!db_escape_literal(db, p->username, esc_user,   sizeof(esc_user)))   return -2;
    if (!db_escape_literal(db, p->password, esc_pwd,    sizeof(esc_pwd)))    return -2;

    snprintf(buf, sizeof(buf),
             "INSERT INTO t_patient "
             "(patient_id, name, gender, age, id_card, phone, address, "
             " username, password, status) "
             "VALUES ('%s', %s, %s, %d, %s, %s, %s, %s, %s, 1)",
             p->patient_id, esc_name, esc_gender, p->age,
             esc_card, esc_phone, esc_addr, esc_user, esc_pwd);

    if (db_execute(db, buf) < 0) {
        if (mysql_errno(db->mysql) == 1062) return -3;
        return -2;
    }

    snprintf(buf, sizeof(buf),
             "SELECT DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_patient WHERE patient_id = '%s'", p->patient_id);
    res = db_query(db, buf);
    if (res) {
        row = db_fetch_row(res);
        if (row) {
            snprintf(p->create_time, sizeof(p->create_time), "%s", db_row_str(res, row, 0));
        }
        db_res_free(res);
    }

    p->status = 1;
    return 0;
}

int patient_update_self(DB_HANDLE* db, const char* patient_id,
                        const char* phone, const char* address) {
    char buf[PAT_SQL_BUF];
    char esc_pid[64];
    char esc_phone[64];
    char esc_addr[448];

    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -4;

    int ok = patient_exists(db, patient_id);
    if (ok < 0) return -2;
    if (ok == 0) return -4;

    if (!db_escape_literal(db, patient_id, esc_pid,   sizeof(esc_pid)))   return -2;
    if (!db_escape_literal(db, phone,      esc_phone, sizeof(esc_phone))) return -2;
    if (!db_escape_literal(db, address,    esc_addr,  sizeof(esc_addr)))  return -2;

    snprintf(buf, sizeof(buf),
             "UPDATE t_patient SET phone = %s, address = %s "
             "WHERE patient_id = %s AND status = 1",
             esc_phone, esc_addr, esc_pid);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

int patient_query_by_id(DB_HANDLE* db, const char* patient_id, Patient* out) {
    char buf[512];
    char esc[64];
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0] || !out) return -4;

    if (!db_escape_literal(db, patient_id, esc, sizeof(esc))) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT patient_id, name, gender, age, id_card, phone, address, "
             "username, password, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s'), status "
             "FROM t_patient WHERE patient_id = %s", esc);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->patient_id,  sizeof(out->patient_id),  "%s", db_row_str(res, row, 0));
    snprintf(out->name,        sizeof(out->name),        "%s", db_row_str(res, row, 1));
    snprintf(out->gender,      sizeof(out->gender),      "%s", db_row_str(res, row, 2));
    out->age = db_row_int(res, row, 3);
    snprintf(out->id_card,     sizeof(out->id_card),     "%s", db_row_str(res, row, 4));
    snprintf(out->phone,       sizeof(out->phone),       "%s", db_row_str(res, row, 5));
    snprintf(out->address,     sizeof(out->address),     "%s", db_row_str(res, row, 6));
    snprintf(out->username,    sizeof(out->username),    "%s", db_row_str(res, row, 7));
    snprintf(out->password,    sizeof(out->password),    "%s", db_row_str(res, row, 8));
    snprintf(out->create_time, sizeof(out->create_time), "%s", db_row_str(res, row, 9));
    out->status = db_row_int(res, row, 10);

    db_res_free(res);
    return 0;
}

int patient_query_all(DB_HANDLE* db, Patient** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return patient_load(db, "", out_list, out_count);
}

void patient_list_free(Patient* list) {
    free(list);
}

int patient_delete(DB_HANDLE* db, const char* patient_id) {
    char buf[512];
    char esc[64];
    int  affected;

    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -4;

    if (!db_escape_literal(db, patient_id, esc, sizeof(esc))) return -2;

    snprintf(buf, sizeof(buf),
             "UPDATE t_patient SET status = 0 "
             "WHERE patient_id = %s AND status = 1", esc);

    affected = db_execute(db, buf);
    if (affected < 0) return -2;
    if (affected == 0) return -4;
    return 0;
}

int patient_export(DB_HANDLE* db, const char* filename) {
    Patient* list = NULL;
    int      count = 0;
    FILE*    fp;
    int      ret;
    int      i;

    if (!db || !db->is_connected) return -1;
    if (!filename || !filename[0]) return -3;

    ret = patient_query_all(db, &list, &count);
    if (ret != 0) return -2;

    fp = fopen(filename, "w");
    if (!fp) {
        patient_list_free(list);
        return -3;
    }

    fprintf(fp, "患者编号\t姓名\t性别\t年龄\t身份证号\t电话\t住址\t注册时间\t状态\n");
    for (i = 0; i < count; i++) {
        fprintf(fp, "%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\n",
                list[i].patient_id, list[i].name, list[i].gender, list[i].age,
                list[i].id_card, list[i].phone, list[i].address,
                list[i].create_time,
                list[i].status == 1 ? "正常" : "已注销");
    }

    if (fclose(fp) != 0) {
        patient_list_free(list);
        return -3;
    }
    patient_list_free(list);
    return 0;
}


static int patient_exists(DB_HANDLE* db, const char* patient_id) {
    char buf[256];
    char esc[64];
    DB_RES* res;
    DB_ROW* row;

    if (!db_escape_literal(db, patient_id, esc, sizeof(esc))) return -1;

    snprintf(buf, sizeof(buf),
             "SELECT patient_id FROM t_patient WHERE patient_id = %s AND status = 1",
             esc);

    res = db_query(db, buf);
    if (!res) return -1;

    row = db_fetch_row(res);
    {
        int found = row ? 1 : 0;
        db_res_free(res);
        return found;
    }
}

static int patient_load(DB_HANDLE* db, const char* where,
                        Patient** out_list, int* out_count) {
    char     buf[PAT_SQL_BUF];
    DB_RES*  res;
    DB_ROW*  row;
    Patient* arr = NULL;
    int      n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT patient_id, name, gender, age, id_card, phone, address, "
             "username, password, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s'), status "
             "FROM t_patient %s ORDER BY patient_id",
             where ? where : "");

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Patient*)malloc(sizeof(Patient) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            Patient* p;

            if (n >= res->row_cnt) break;

            p = &arr[n];
            memset(p, 0, sizeof(*p));
            snprintf(p->patient_id,  sizeof(p->patient_id),  "%s", db_row_str(res, row, 0));
            snprintf(p->name,        sizeof(p->name),        "%s", db_row_str(res, row, 1));
            snprintf(p->gender,      sizeof(p->gender),      "%s", db_row_str(res, row, 2));
            p->age = db_row_int(res, row, 3);
            snprintf(p->id_card,     sizeof(p->id_card),     "%s", db_row_str(res, row, 4));
            snprintf(p->phone,       sizeof(p->phone),       "%s", db_row_str(res, row, 5));
            snprintf(p->address,     sizeof(p->address),     "%s", db_row_str(res, row, 6));
            snprintf(p->username,    sizeof(p->username),    "%s", db_row_str(res, row, 7));
            snprintf(p->password,    sizeof(p->password),    "%s", db_row_str(res, row, 8));
            snprintf(p->create_time, sizeof(p->create_time), "%s", db_row_str(res, row, 9));
            p->status = db_row_int(res, row, 10);
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}
