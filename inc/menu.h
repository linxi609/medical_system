
#ifndef MENU_H
#define MENU_H

#include "db_mysql.h"
#include "auth.h"

void menu_admin(DB_HANDLE* db, const LoginUser* user);

void menu_doctor(DB_HANDLE* db, const LoginUser* user);

void menu_patient(DB_HANDLE* db, const LoginUser* user);

#endif
