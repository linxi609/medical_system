
#ifndef APPOINTMENT_H
#define APPOINTMENT_H

#include "db_mysql.h"

typedef struct {
    int  appoint_id;
    char patient_id[20];
    char doctor_id[10];
    char appoint_date[12];
    char time_slot[10];
    char symptom[200];
    int  status;
    char create_time[20];
} Appointment;
int appointment_add(DB_HANDLE* db, Appointment* a);
int appointment_cancel(DB_HANDLE* db, int appoint_id, const char* patient_id);
int appointment_query_by_patient(DB_HANDLE* db, const char* patient_id, Appointment** out_list, int* out_count);
int appointment_query_by_doctor_today(DB_HANDLE* db, const char* doctor_id,Appointment** out_list, int* out_count);
int appointment_query_all(DB_HANDLE* db, Appointment** out_list, int* out_count);
void appointment_list_free(Appointment* list);

#endif
