
#include "auth.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int    role;
    char   username[64];
    int    fails;
    time_t lock_until;
} LoginAttempt;

static LoginAttempt s_attempts[32];

static LoginAttempt* attempt_slot(int role, const char* username, int create) {
    time_t now = time(NULL);

    for (int i = 0; i < 32; i++) {
        LoginAttempt* a = &s_attempts[i];
        if (a->username[0] != '\0' && a->role == role &&
            strcmp(a->username, username) == 0) {
            if (a->lock_until != 0 && now >= a->lock_until) {
                a->fails      = 0;
                a->lock_until = 0;
            }
            return a;
        }
    }
    if (!create) return NULL;

    LoginAttempt* victim = NULL;
    for (int i = 0; i < 32; i++) {
        LoginAttempt* a = &s_attempts[i];
        if (a->username[0] == '\0') { victim = a; break; }
        if (!victim || a->lock_until < victim->lock_until) victim = a;
    }
    memset(victim, 0, sizeof(*victim));
    victim->role = role;
    snprintf(victim->username, sizeof(victim->username), "%s", username);
    return victim;
}

static int record_failure(int role, const char* username) {
    LoginAttempt* a = attempt_slot(role, username, 1);
    a->fails++;
    if (a->fails >= 5) {
        a->lock_until = time(NULL) + 600;
        return -7;
    }
    return -4;
}

static void record_success(int role, const char* username) {
    LoginAttempt* a = attempt_slot(role, username, 0);
    if (a) {
        a->fails      = 0;
        a->lock_until = 0;
        a->username[0] = '\0';
    }
}




static const char* user_table(int role) {
    if (role == 1) return "t_admin";
    if (role == 2) return "t_doctor";
    return "t_patient";
}

static const char* user_id_col(int role) {
    if (role == 1) return "admin_id";
    if (role == 2) return "doctor_id";
    return "patient_id";
}

static int fetch_user_by_name(DB_HANDLE* db, int role, const char* username,
                              char* id_out, size_t id_size,
                              char* name_out, size_t name_size,
                              char* pwd_out, size_t pwd_size,
                              int* status_out) {
    char esc[128];
    char sql[512];

    if (!db_escape(db, username, esc, sizeof(esc))) return -2;

    if (role == 1) {
        snprintf(sql, sizeof(sql),
                 "SELECT admin_id, name, password FROM t_admin WHERE username='%s'",
                 esc);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT %s, name, password, status FROM %s WHERE username='%s'",
                 user_id_col(role), user_table(role), esc);
    }

    DB_RES* res = db_query(db, sql);
    if (!res) return -2;

    DB_ROW* row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return 1;
    }
    copy_str(id_out,   id_size,   row->fields[0]);
    copy_str(name_out, name_size, row->fields[1]);
    copy_str(pwd_out,  pwd_size,  row->fields[2]);
    if (role == 1) {
        *status_out = 1;
    } else {
        *status_out = row->fields[3] ? atoi(row->fields[3]) : 0;
    }

    db_res_free(res);
    return 0;
}


int user_login(DB_HANDLE* db, int role, const char* username,
               const char* password, LoginUser* out) {
    if (!db || !db->is_connected) return -1;
    if (role < 1 || role > 3)     return -6;
    if (!username || !password || !out) return -2;

    LoginAttempt* att = attempt_slot(role, username, 0);
    if (att && att->lock_until != 0 && time(NULL) < att->lock_until) return -7;

    char id[20];
    char name[50];
    char stored[160];
    int  status = 1;

    int ret = fetch_user_by_name(db, role, username,
                                 id, sizeof(id), name, sizeof(name),
                                 stored, sizeof(stored), &status);
    if (ret == -2) return -2;

    if (ret == 1) {
        for (int other = 1; other <= 3; other++) {
            if (other == role) continue;
            char oid[32], oname[64], opwd[160];
            int  ostatus = 1;
            int r2 = fetch_user_by_name(db, other, username,
                                        oid, sizeof(oid), oname, sizeof(oname),
                                        opwd, sizeof(opwd), &ostatus);
            if (r2 == -2) return -2;
            if (r2 == 0)  return -6;
        }
        return record_failure(role, username);
    }

    if (status != 1) return -7;

    char* hashed = hash_password(password);
    if (!hashed) return -2;
    int pwd_ok = (strcmp(hashed, stored) == 0);
    free(hashed);
    if (!pwd_ok) return record_failure(role, username);

    record_success(role, username);
    snprintf(out->id,   sizeof(out->id),   "%s", id);
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->role = role;
    return 0;
}

int user_register_patient(DB_HANDLE* db, Patient* p) {
    if (!db || !db->is_connected) return -1;
    if (!p) return -2;

    char esc_card[64];
    if (!db_escape(db, p->id_card, esc_card, sizeof(esc_card))) return -2;

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT patient_id FROM t_patient WHERE id_card='%s' LIMIT 1",
             esc_card);

    DB_RES* res = db_query(db, sql);
    if (!res) return -2;
    int dup = (db_fetch_row(res) != NULL);
    db_res_free(res);
    if (dup) return -3;

    p->status = 1;
    return patient_register(db, p);
}

int user_change_password(DB_HANDLE* db, int role, const char* id,
                         const char* old_pwd, const char* new_pwd) {
    if (!db || !db->is_connected) return -1;
    if (role < 1 || role > 3)     return -6;
    if (!id || !old_pwd || !new_pwd) return -2;

    char esc_id[64];
    char esc_new[224];
    if (!db_escape(db, id,      esc_id,  sizeof(esc_id))) return -2;
    if (!db_escape(db, new_pwd, esc_new, sizeof(esc_new))) return -2;

    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT password FROM %s WHERE %s='%s'",
             user_table(role), user_id_col(role), esc_id);

    DB_RES* res = db_query(db, sql);
    if (!res) return -2;

    DB_ROW* row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }
    char stored[160];
    copy_str(stored, sizeof(stored), row->fields[0]);
    db_res_free(res);

    char* hashed = hash_password(old_pwd);
    if (!hashed) return -2;
    int pwd_ok = (strcmp(hashed, stored) == 0);
    free(hashed);
    if (!pwd_ok) return -4;

    snprintf(sql, sizeof(sql), "UPDATE %s SET password='%s' WHERE %s='%s'",
             user_table(role), esc_new, user_id_col(role), esc_id);
    if (db_execute(db, sql) < 0) return -2;

    return 0;
}

char* hash_password(const char* plain) {
    if (!plain) return NULL;
    char* out = (char*)malloc(strlen(plain) + 1);
    if (!out) return NULL;
    strcpy(out, plain);
    return out;
}
