
#ifndef MEDICAL_RECORD_H
#define MEDICAL_RECORD_H

#include "db_mysql.h"

typedef struct {
    int  record_id;
    char patient_id[20];
    char doctor_id[10];
    int  visit_id;
    char allergy_history[200];
    char past_history[200];
    char family_history[200];
    char update_time[20];
} MedicalRecord;

int medical_record_add(DB_HANDLE* db, MedicalRecord* r);

int medical_record_update(DB_HANDLE* db, const MedicalRecord* r);

int medical_record_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                    MedicalRecord** out_list, int* out_count);

int medical_record_query_all(DB_HANDLE* db, MedicalRecord** out_list, int* out_count);

void medical_record_list_free(MedicalRecord* list);

#endif
