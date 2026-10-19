#ifndef MENU_COMMON_H
#define MENU_COMMON_H

#include "db_mysql.h"
#include "auth.h"

const char* err_text(int ret);
const char* appt_status_text(int status);
const char* presc_status_text(int status);
void menu_change_password(DB_HANDLE* db, const LoginUser* user);

#endif
