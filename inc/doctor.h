
#ifndef DOCTOR_H
#define DOCTOR_H

#include "db_mysql.h"

typedef struct {
    char doctor_id[10];
    char name[50];
    char gender[12];
    char title[20];
    char dept[50];
    char phone[20];
    char username[50];
    char password[100];
    int  status;
} Doctor;

int doctor_add(DB_HANDLE* db, Doctor* d);

int doctor_delete(DB_HANDLE* db, const char* doctor_id);

int doctor_update(DB_HANDLE* db, const Doctor* d);

int doctor_query_by_id(DB_HANDLE* db, const char* doctor_id, Doctor* out);

int doctor_query_all(DB_HANDLE* db, Doctor** out_list, int* out_count);

void doctor_list_free(Doctor* list);

#endif
