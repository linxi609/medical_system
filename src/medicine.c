/* ==========================================================================
 * medicine.c —— M06 药品库存管理 实现
 * --------------------------------------------------------------------------
 * 负责组：管理员组
 * 对应头文件：inc/medicine.h
 * 说明：函数声明和返回值含义见 medicine.h，本文件只填实现。
 *       库存低于 50 视为不足，判断时直接写 50。
 *
 * 【当前进度】medicine_query_all / medicine_query_by_id / medicine_list_free
 *   先前写在 src/doctor_deps.c（医生组开方要查药，临时顶替），2026-10-13
 *   经维护者搬回本文件、删掉了 doctor_deps.c 里的重复实现，项目才能链接。
 *   管理员组写入库/出库/调价时，可一并复核这三个查询函数；行为要改的话，
 *   医生工作台依赖"medicine_query_by_id 查不到返回 -4"这一点。
 * ========================================================================== */

#include "medicine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * 本文件用到的小工具
 * -------------------------------------------------------------------------- */

/* 把字符串转义后拼成 SQL 字面量（带两边的单引号），写进 dst。
 * 直接拼字符串的话，用户输入里带个单引号就会把 SQL 拼坏，所以统一走一遍
 * mysql_real_escape_string。src 传 NULL 时按空字符串处理。
 * dst 装不下时返回 NULL，调用方跳过即可。 */
static char* escape_str(DB_HANDLE* db, const char* src, char* dst, size_t dstsz) {
    size_t n;
    if (!dst || dstsz < 3) return NULL;
    if (!src) src = "";
    n = mysql_real_escape_string(db->mysql, dst + 1, src, (unsigned long)strlen(src));
    if (n + 3 > dstsz) return NULL;
    dst[0] = '\'';
    dst[n + 1] = '\'';
    dst[n + 2] = '\0';
    return dst;
}

/* 取一行里第 idx 列，NULL 列或越界都返回空字符串 */
static const char* col_str(DB_RES* res, DB_ROW* row, int idx) {
    if (!row || !row->fields) return "";
    if (idx < 0 || idx >= row->col_cnt || idx >= res->col_cnt) return "";
    if (!row->fields[idx]) return "";
    return row->fields[idx];
}

/* 取一行里第 idx 列当整数用 */
static int col_int(DB_RES* res, DB_ROW* row, int idx) {
    const char* s = col_str(res, row, idx);
    return s[0] ? atoi(s) : 0;
}

/* 取一行里第 idx 列当小数用 */
static double col_double(DB_RES* res, DB_ROW* row, int idx) {
    const char* s = col_str(res, row, idx);
    return s[0] ? atof(s) : 0.0;
}

/* ==========================================================================
 * 管理员组负责的函数（还是空壳，等管理员组填）
 * ========================================================================== */

int medicine_add(DB_HANDLE* db, const Medicine* m) {
    (void)db; (void)m;
    return 0;  /* TODO: INSERT INTO t_medicine ... */
}

int medicine_stock_in(DB_HANDLE* db, const char* med_id, int quantity) {
    (void)db; (void)med_id; (void)quantity;
    return 0;  /* TODO: UPDATE t_medicine SET stock=stock+? WHERE med_id=? */
}

int medicine_stock_out(DB_HANDLE* db, const char* med_id, int quantity) {
    (void)db; (void)med_id; (void)quantity;
    return 0;  /* TODO: 先查库存确认够扣，再 UPDATE stock=stock-?（不足返回 -5） */
}

int medicine_update_price(DB_HANDLE* db, const char* med_id, double new_price) {
    (void)db; (void)med_id; (void)new_price;
    return 0;  /* TODO: UPDATE t_medicine SET price=? WHERE med_id=? */
}

/* ==========================================================================
 * 医生组先实现的查询（原 doctor_deps.c）
 * --------------------------------------------------------------------------
 * 查全部药品按药品编号排序，列表展示稳定，医生翻库存时好找。
 * 库存不足的判断不在这里做，由调用方按 stock < 50 标注
 * （教程 1.4 M06：列表展示时库存低于 50 的行标注"库存不足"）。
 * ========================================================================== */
int medicine_query_all(DB_HANDLE* db, Medicine** out_list, int* out_count) {
    DB_RES* res;
    DB_ROW* row;
    Medicine* arr = NULL;
    int n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    res = db_query(db,
                   "SELECT med_id, med_name, spec, unit, price, stock, manufacturer "
                   "FROM t_medicine ORDER BY med_id");
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Medicine*)malloc(sizeof(Medicine) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            Medicine* m;

            /* 双保险：数组按 res->row_cnt 分配，理论上取不到这么多行；
             * 万一结果集比报出来的行数还多，宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            m = &arr[n];
            memset(m, 0, sizeof(*m));
            snprintf(m->med_id,       sizeof(m->med_id),       "%s", col_str(res, row, 0));
            snprintf(m->med_name,     sizeof(m->med_name),     "%s", col_str(res, row, 1));
            snprintf(m->spec,         sizeof(m->spec),         "%s", col_str(res, row, 2));
            snprintf(m->unit,         sizeof(m->unit),         "%s", col_str(res, row, 3));
            m->price = col_double(res, row, 4);
            m->stock = col_int(res, row, 5);
            snprintf(m->manufacturer, sizeof(m->manufacturer), "%s", col_str(res, row, 6));
            n++;
        }
    }
    db_res_free(res);

    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}

/* 按编号查单个药品（开方时逐条校验用）。查不到返回 -4 */
int medicine_query_by_id(DB_HANDLE* db, const char* med_id, Medicine* out) {
    char buf[512];
    char esc[128];
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!med_id || !med_id[0] || !out) return -4;

    if (!escape_str(db, med_id, esc, sizeof(esc))) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT med_id, med_name, spec, unit, price, stock, manufacturer "
             "FROM t_medicine WHERE med_id = %s", esc);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {                       /* 没这个药品 */
        db_res_free(res);
        return -4;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->med_id,       sizeof(out->med_id),       "%s", col_str(res, row, 0));
    snprintf(out->med_name,     sizeof(out->med_name),     "%s", col_str(res, row, 1));
    snprintf(out->spec,         sizeof(out->spec),         "%s", col_str(res, row, 2));
    snprintf(out->unit,         sizeof(out->unit),         "%s", col_str(res, row, 3));
    out->price = col_double(res, row, 4);
    out->stock = col_int(res, row, 5);
    snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", col_str(res, row, 6));

    db_res_free(res);
    return 0;
}

void medicine_list_free(Medicine* list) {
    free(list);
}
