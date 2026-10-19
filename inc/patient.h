
#ifndef PATIENT_H
#define PATIENT_H

#include "db_mysql.h"

typedef struct {
    char patient_id[20];
    char name[50];
    char gender[12];
    int  age;
    char id_card[19];
    char phone[20];
    char address[200];
    char username[50];
    char password[100];
    char create_time[20];
    int  status;
} Patient;

int patient_register(DB_HANDLE* db, Patient* p);

int patient_update_self(DB_HANDLE* db, const char* patient_id,
                        const char* phone, const char* address);

int patient_query_by_id(DB_HANDLE* db, const char* patient_id, Patient* out);

int patient_query_all(DB_HANDLE* db, Patient** out_list, int* out_count);

void patient_list_free(Patient* list);

int patient_delete(DB_HANDLE* db, const char* patient_id);

int patient_export(DB_HANDLE* db, const char* filename);

#endif
