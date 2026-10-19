
#ifndef PRESCRIPTION_H
#define PRESCRIPTION_H

#include "db_mysql.h"

typedef struct {
    int  presc_id;
    int  visit_id;
    char patient_id[20];
    char doctor_id[10];
    char diagnosis[200];
    int  status;
    char create_time[20];
} Prescription;

typedef struct {
    int    detail_id;
    int    presc_id;
    char   med_id[20];
    int    quantity;
    char   dosage[100];
    double unit_price;
    double subtotal;
} PrescDetail;

int prescription_add(DB_HANDLE* db, Prescription* p,
                     PrescDetail* items, int item_count);

int prescription_void(DB_HANDLE* db, int presc_id, const char* doctor_id);

int prescription_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                  Prescription** out_list, int* out_count);

int prescription_query_by_id(DB_HANDLE* db, int presc_id,
                             Prescription* out,
                             PrescDetail** out_items, int* out_item_count);

int prescription_query_all(DB_HANDLE* db, Prescription** out_list, int* out_count);

void prescription_list_free(Prescription* list);

void prescription_detail_free(PrescDetail* items);

#endif
