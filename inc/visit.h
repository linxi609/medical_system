
#ifndef VISIT_H
#define VISIT_H

#include "db_mysql.h"

typedef struct {
    int  visit_id;
    char patient_id[20];
    char doctor_id[10];
    int  appoint_id;
    char visit_time[20];
    char chief_complaint[500];
    char present_illness[500];
    double temp;
    char blood_pressure[10];
    int  heart_rate;
} VisitRecord;

int visit_add(DB_HANDLE* db, VisitRecord* v);

int visit_query_by_patient(DB_HANDLE* db, const char* patient_id,
                           VisitRecord** out_list, int* out_count);

int visit_query_by_doctor(DB_HANDLE* db, const char* doctor_id,
                          VisitRecord** out_list, int* out_count);

int visit_query_all(DB_HANDLE* db, VisitRecord** out_list, int* out_count);

void visit_list_free(VisitRecord* list);

#endif
