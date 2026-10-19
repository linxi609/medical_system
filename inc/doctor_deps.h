
#ifndef DOCTOR_DEPS_H
#define DOCTOR_DEPS_H

#include "db_mysql.h"
#include "medical_record.h"

int medical_record_find_by_visit(DB_HANDLE* db, int visit_id, MedicalRecord* out);

int medical_record_find_by_id(DB_HANDLE* db, int record_id, MedicalRecord* out);

#endif
