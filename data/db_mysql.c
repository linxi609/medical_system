#include "db_mysql.h"

DB_HANDLE* db_create(void) {
    DB_HANDLE* h = (DB_HANDLE*)malloc(sizeof(DB_HANDLE));
    if (!h) return NULL;
    h->mysql = NULL;
    h->is_connected = 0;
    return h;
}

int db_connect(DB_HANDLE* h, const char* host, const char* user,
               const char* passwd, const char* dbname, int port) {
    if (!h) return -1;
    h->mysql = mysql_init(NULL);
    if (!h->mysql) { printf("mysql_init error\n"); return -1; }
    if (!mysql_real_connect(h->mysql, host, user, passwd, dbname, port, NULL, 0)) {
        printf("connect error: %s\n", mysql_error(h->mysql));
        mysql_close(h->mysql);
        h->mysql = NULL;
        return -1;
    }
    mysql_set_character_set(h->mysql, "utf8mb4");
    h->is_connected = 1;
    printf("db connect success\n");
    return 0;
}

void db_close(DB_HANDLE* h) {
    if (!h) return;
    if (h->mysql) { mysql_close(h->mysql); h->mysql = NULL; }
    h->is_connected = 0;
    free(h);
}

int db_execute(DB_HANDLE* h, const char* sql) {
    if (!h || !h->is_connected || !sql) return -1;
    if (mysql_query(h->mysql, sql)) {
        printf("execute error: %s\n", mysql_error(h->mysql));
        return -1;
    }
    return (int)mysql_affected_rows(h->mysql);
}

DB_RES* db_query(DB_HANDLE* h, const char* sql) {
    if (!h || !h->is_connected || !sql) return NULL;
    if (mysql_query(h->mysql, sql)) {
        printf("query error: %s\n", mysql_error(h->mysql));
        return NULL;
    }
    MYSQL_RES* res = mysql_store_result(h->mysql);
    if (!res) return NULL;

    DB_RES* db_res = (DB_RES*)malloc(sizeof(DB_RES));
    if (!db_res) { mysql_free_result(res); return NULL; }
    db_res->res     = res;
    db_res->row_cnt = (int)mysql_num_rows(res);
    db_res->col_cnt = (int)mysql_num_fields(res);
    return db_res;
}

DB_ROW* db_fetch_row(DB_RES* res) {
    if (!res || !res->res) return NULL;
    MYSQL_ROW row = mysql_fetch_row(res->res);
    if (!row) return NULL;
    static DB_ROW db_row;
    db_row.fields  = row;
    db_row.col_cnt = res->col_cnt;
    return &db_row;
}

void db_res_free(DB_RES* res) {
    if (!res) return;
    if (res->res) mysql_free_result(res->res);
    free(res);
}

char* db_escape(DB_HANDLE* h, const char* src, char* dst, size_t dstsz) {
    size_t n;
    if (!h || !h->mysql || !dst || dstsz == 0) return NULL;
    if (!src) src = "";
    n = strlen(src);
    if (n > (dstsz - 1) / 2) return NULL;
    mysql_real_escape_string(h->mysql, dst, src, (unsigned long)n);
    return dst;
}

char* db_escape_literal(DB_HANDLE* h, const char* src, char* dst, size_t dstsz) {
    size_t n;
    if (!dst || dstsz < 3) return NULL;
    if (!db_escape(h, src, dst + 1, dstsz - 2)) return NULL;
    n = strlen(dst + 1);
    dst[0] = '\'';
    dst[n + 1] = '\'';
    dst[n + 2] = '\0';
    return dst;
}

const char* db_row_str(DB_RES* res, DB_ROW* row, int idx) {
    if (!row || !row->fields) return "";
    if (idx < 0 || idx >= row->col_cnt || idx >= res->col_cnt) return "";
    if (!row->fields[idx]) return "";
    return row->fields[idx];
}

int db_row_int(DB_RES* res, DB_ROW* row, int idx) {
    const char* s = db_row_str(res, row, idx);
    return s[0] ? atoi(s) : 0;
}

double db_row_double(DB_RES* res, DB_ROW* row, int idx) {
    const char* s = db_row_str(res, row, idx);
    return s[0] ? atof(s) : 0.0;
}

int db_begin_tx(DB_HANDLE* h) {
    if (!h || !h->is_connected) return -1;
    if (db_execute(h, "BEGIN") < 0) return -2;
    return 0;
}
