
#ifndef AUTH_H
#define AUTH_H

#include "db_mysql.h"
#include "patient.h"

typedef struct {
    char id[20];
    char name[50];
    int  role;
} LoginUser;

int user_login(DB_HANDLE* db, int role, const char* username,
               const char* password, LoginUser* out);

int user_register_patient(DB_HANDLE* db, Patient* p);

int user_change_password(DB_HANDLE* db, int role, const char* id,
                         const char* old_pwd, const char* new_pwd);

char* hash_password(const char* plain);

#endif
