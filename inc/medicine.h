
#ifndef MEDICINE_H
#define MEDICINE_H

#include "db_mysql.h"

typedef struct {
    char   med_id[20];
    char   med_name[50];
    char   spec[50];
    char   unit[10];
    double price;
    int    stock;
    char   manufacturer[50];
} Medicine;

int medicine_add(DB_HANDLE* db, Medicine* m);

int medicine_stock_in(DB_HANDLE* db, const char* med_id, int quantity);

int medicine_stock_out(DB_HANDLE* db, const char* med_id, int quantity);

int medicine_update_price(DB_HANDLE* db, const char* med_id, double new_price);

int medicine_delete(DB_HANDLE* db, const char* med_id);

int medicine_query_all(DB_HANDLE* db, Medicine** out_list, int* out_count);

int medicine_query_by_id(DB_HANDLE* db, const char* med_id, Medicine* out);

void medicine_list_free(Medicine* list);

#endif
