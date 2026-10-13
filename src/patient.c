/* ==========================================================================
 * patient.c —— M01 患者档案管理 实现
 * --------------------------------------------------------------------------
 * 负责组：患者组
 * 对应头文件：inc/patient.h
 * 对应表：t_patient
 *
 * 实现说明：
 *   1) 患者编号按 "P + 年份 + 4 位流水号" 生成：查当年最大编号加 1，
 *      年份取数据库的 YEAR(NOW())，和 create_time 用同一个时钟。
 *      生成到插入之间没有加锁——注册前先由 user_register_patient 校验
 *      账号/身份证号唯一，撞号的概率极低；真撞上了唯一索引会拦下，
 *      按 -3 处理即可。
 *   2) 注销是软删除（status=0），唯一键（username/id_card）不释放，
 *      所以查询患者时一律带上 status=1 才算"在册"。
 *   3) 登录账号（username）不由患者自己填，直接取患者编号：编号由系统生成、
 *      不会重复，注销后账号继续占着位子也就不是问题了（编号本来就不该复用）。
 *   4) 所有外部输入拼进 SQL 前都过 escape_str() 转义。
 * ========================================================================== */

#include "patient.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAT_SQL_BUF 4096

/* --------------------------------------------------------------------------
 * 本文件用到的小工具（写法与 visit.c / appointment.c 保持一致）
 * -------------------------------------------------------------------------- */

/* 把字符串转义后拼成 SQL 字面量（带两边的单引号），写进 dst。
 * src 传 NULL 时按空字符串处理；dst 装不下时返回 NULL。 */
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

/* --------------------------------------------------------------------------
 * 内部函数声明
 * -------------------------------------------------------------------------- */
static int patient_exists(DB_HANDLE* db, const char* patient_id);
static int patient_load(DB_HANDLE* db, const char* where,
                        Patient** out_list, int* out_count);

/* ==========================================================================
 * 患者注册建档
 * 返回：0 成功；-3 身份证号或账号已存在；-2 SQL 执行失败；-1 连接失败
 * ========================================================================== */
int patient_register(DB_HANDLE* db, Patient* p) {
    char    buf[PAT_SQL_BUF];
    char    esc_name[128];
    char    esc_gender[32];
    char    esc_card[64];
    char    esc_phone[64];
    char    esc_addr[448];
    char    esc_user[128];
    char    esc_pwd[224];
    DB_RES* res;
    DB_ROW* row;
    int     seq = 0;

    if (!db || !db->is_connected) return -1;
    if (!p) return -2;

    /* ---------- 生成编号：P + 年份 + 4 位流水号 ----------
     * 一条 SQL 同时拿到年份和当年最大流水号，加 1 即为新编号。
     * 当年还没有患者时 MAX 是 NULL，流水号从 1 开始。
     * 年份从数据库取，和 create_time 的 DEFAULT CURRENT_TIMESTAMP 用同一个时钟。 */
    res = db_query(db,
                   "SELECT MAX(CAST(SUBSTRING(patient_id, 6) AS UNSIGNED)) "
                   "FROM t_patient "
                   "WHERE patient_id LIKE CONCAT('P', YEAR(NOW()), '%')");
    if (!res) return -2;

    row = db_fetch_row(res);
    if (row && row->fields[0]) seq = atoi(row->fields[0]);
    db_res_free(res);
    seq++;

    /* 年份从数据库取，和 create_time 的 DEFAULT CURRENT_TIMESTAMP 用同一个时钟 */
    {
        char year[8] = "";
        res = db_query(db, "SELECT YEAR(NOW())");
        if (!res) return -2;
        row = db_fetch_row(res);
        if (row && row->fields[0]) snprintf(year, sizeof(year), "%s", row->fields[0]);
        db_res_free(res);
        if (!year[0]) return -2;
        snprintf(p->patient_id, sizeof(p->patient_id), "P%s%04d", year, seq);
    }

    /* ---------- 登录账号 = 患者编号 ----------
     * 不让患者自己起账号名：编号是系统生成的、天然唯一，也就不存在
     * "注销后账号还被占着、同名注册不进来"的问题（编号本来就不该复用）。 */
    snprintf(p->username, sizeof(p->username), "%s", p->patient_id);

    /* ---------- 转义各字段 ---------- */
    if (!escape_str(db, p->name,     esc_name,   sizeof(esc_name)))   return -2;
    if (!escape_str(db, p->gender,   esc_gender, sizeof(esc_gender))) return -2;
    if (!escape_str(db, p->id_card,  esc_card,   sizeof(esc_card)))   return -2;
    if (!escape_str(db, p->phone,    esc_phone,  sizeof(esc_phone)))  return -2;
    if (!escape_str(db, p->address,  esc_addr,   sizeof(esc_addr)))   return -2;
    if (!escape_str(db, p->username, esc_user,   sizeof(esc_user)))   return -2;
    if (!escape_str(db, p->password, esc_pwd,    sizeof(esc_pwd)))    return -2;

    /* ---------- 插入 ----------
     * patient_id 转义后是纯 ASCII（P+数字），直接拼不用转义。
     * create_time 交给 DEFAULT CURRENT_TIMESTAMP。 */
    snprintf(buf, sizeof(buf),
             "INSERT INTO t_patient "
             "(patient_id, name, gender, age, id_card, phone, address, "
             " username, password, status) "
             "VALUES ('%s', %s, %s, %d, %s, %s, %s, %s, %s, 1)",
             p->patient_id, esc_name, esc_gender, p->age,
             esc_card, esc_phone, esc_addr, esc_user, esc_pwd);

    if (db_execute(db, buf) < 0) {
        /* 唯一索引撞了（并发下同流水号 / 账号 / 身份证号重复）按 -3 处理 */
        if (mysql_errno(db->mysql) == 1062) return -3;
        return -2;
    }

    /* ---------- 把数据库认定的注册时间读回来，供调用方展示 ---------- */
    snprintf(buf, sizeof(buf),
             "SELECT DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM t_patient WHERE patient_id = '%s'", p->patient_id);
    res = db_query(db, buf);
    if (res) {
        row = db_fetch_row(res);
        if (row) {
            snprintf(p->create_time, sizeof(p->create_time), "%s", col_str(res, row, 0));
        }
        db_res_free(res);
    }

    p->status = 1;
    return 0;
}

/* ==========================================================================
 * 患者修改本人联系方式（只能改电话和住址）
 * 返回：0 成功；-4 患者不存在或已注销；-2 SQL 执行失败
 * ========================================================================== */
int patient_update_self(DB_HANDLE* db, const char* patient_id,
                        const char* phone, const char* address) {
    char buf[PAT_SQL_BUF];
    char esc_pid[64];
    char esc_phone[64];
    char esc_addr[448];

    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -4;

    /* 先确认患者在册：affected_rows 为 0 可能是"值没变"，不能靠它判断 */
    int ok = patient_exists(db, patient_id);
    if (ok < 0) return -2;
    if (ok == 0) return -4;

    if (!escape_str(db, patient_id, esc_pid,   sizeof(esc_pid)))   return -2;
    if (!escape_str(db, phone,      esc_phone, sizeof(esc_phone))) return -2;
    if (!escape_str(db, address,    esc_addr,  sizeof(esc_addr)))  return -2;

    snprintf(buf, sizeof(buf),
             "UPDATE t_patient SET phone = %s, address = %s "
             "WHERE patient_id = %s AND status = 1",
             esc_phone, esc_addr, esc_pid);

    if (db_execute(db, buf) < 0) return -2;
    /* 新值和旧值相同时 affected_rows 为 0，那也算修改成功 */
    return 0;
}

/* ==========================================================================
 * 按编号查询患者
 * 返回：0 成功；-4 未找到；-2 SQL 执行失败
 * ========================================================================== */
int patient_query_by_id(DB_HANDLE* db, const char* patient_id, Patient* out) {
    char buf[512];
    char esc[64];
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0] || !out) return -4;

    if (!escape_str(db, patient_id, esc, sizeof(esc))) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT patient_id, name, gender, age, id_card, phone, address, "
             "username, password, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s'), status "
             "FROM t_patient WHERE patient_id = %s", esc);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->patient_id,  sizeof(out->patient_id),  "%s", col_str(res, row, 0));
    snprintf(out->name,        sizeof(out->name),        "%s", col_str(res, row, 1));
    snprintf(out->gender,      sizeof(out->gender),      "%s", col_str(res, row, 2));
    out->age = col_int(res, row, 3);
    snprintf(out->id_card,     sizeof(out->id_card),     "%s", col_str(res, row, 4));
    snprintf(out->phone,       sizeof(out->phone),       "%s", col_str(res, row, 5));
    snprintf(out->address,     sizeof(out->address),     "%s", col_str(res, row, 6));
    snprintf(out->username,    sizeof(out->username),    "%s", col_str(res, row, 7));
    snprintf(out->password,    sizeof(out->password),    "%s", col_str(res, row, 8));
    snprintf(out->create_time, sizeof(out->create_time), "%s", col_str(res, row, 9));
    out->status = col_int(res, row, 10);

    db_res_free(res);
    return 0;
}

/* ==========================================================================
 * 查询全部患者（管理员用）
 * 返回：0 成功（可能 out_count 为 0）；-2 SQL 执行失败
 * ========================================================================== */
int patient_query_all(DB_HANDLE* db, Patient** out_list, int* out_count) {
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    return patient_load(db, "", out_list, out_count);
}

/* ==========================================================================
 * 释放 patient_query_all 返回的数组
 * ========================================================================== */
void patient_list_free(Patient* list) {
    free(list);
}

/* ==========================================================================
 * 注销患者（软删除：status 置 0）
 * 返回：0 成功；-4 患者不存在或已注销；-2 SQL 执行失败
 * ========================================================================== */
int patient_delete(DB_HANDLE* db, const char* patient_id) {
    char buf[512];
    char esc[64];
    int  affected;

    if (!db || !db->is_connected) return -1;
    if (!patient_id || !patient_id[0]) return -4;

    if (!escape_str(db, patient_id, esc, sizeof(esc))) return -2;

    /* 条件里带 status = 1：已注销的再注销就是 0 行，按"不存在"处理 */
    snprintf(buf, sizeof(buf),
             "UPDATE t_patient SET status = 0 "
             "WHERE patient_id = %s AND status = 1", esc);

    affected = db_execute(db, buf);
    if (affected < 0) return -2;
    if (affected == 0) return -4;      /* 不存在，或之前已经注销过了 */
    return 0;
}

/* ==========================================================================
 * 导出患者列表到文本文件
 * 返回：0 成功；-2 查询失败；-3 写文件失败（目录不存在、没权限等）
 * 说明：查询失败和写文件失败分开报，菜单层才能给出"到底哪一步错了"的提示
 * ========================================================================== */
int patient_export(DB_HANDLE* db, const char* filename) {
    Patient* list = NULL;
    int      count = 0;
    FILE*    fp;
    int      ret;
    int      i;

    if (!db || !db->is_connected) return -1;
    if (!filename || !filename[0]) return -3;

    ret = patient_query_all(db, &list, &count);
    if (ret != 0) return -2;

    fp = fopen(filename, "w");
    if (!fp) {
        patient_list_free(list);
        return -3;
    }

    fprintf(fp, "患者编号\t姓名\t性别\t年龄\t身份证号\t电话\t住址\t注册时间\t状态\n");
    for (i = 0; i < count; i++) {
        fprintf(fp, "%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\n",
                list[i].patient_id, list[i].name, list[i].gender, list[i].age,
                list[i].id_card, list[i].phone, list[i].address,
                list[i].create_time,
                list[i].status == 1 ? "正常" : "已注销");
    }

    if (fclose(fp) != 0) {
        patient_list_free(list);
        return -3;                     /* 写盘出错（磁盘满、配额等） */
    }
    patient_list_free(list);
    return 0;
}

/* ==========================================================================
 * 内部函数
 * ========================================================================== */

/* 患者是否在册（status=1）：1 在册，0 不存在或已注销，-1 SQL 失败 */
static int patient_exists(DB_HANDLE* db, const char* patient_id) {
    char buf[256];
    char esc[64];
    DB_RES* res;
    DB_ROW* row;

    if (!escape_str(db, patient_id, esc, sizeof(esc))) return -1;

    snprintf(buf, sizeof(buf),
             "SELECT patient_id FROM t_patient WHERE patient_id = %s AND status = 1",
             esc);

    res = db_query(db, buf);
    if (!res) return -1;

    row = db_fetch_row(res);
    {
        int found = row ? 1 : 0;
        db_res_free(res);
        return found;
    }
}

/* 结果集 -> Patient 数组（patient_query_all 用；where 传空串表示不加条件） */
static int patient_load(DB_HANDLE* db, const char* where,
                        Patient** out_list, int* out_count) {
    char     buf[PAT_SQL_BUF];
    DB_RES*  res;
    DB_ROW*  row;
    Patient* arr = NULL;
    int      n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT patient_id, name, gender, age, id_card, phone, address, "
             "username, password, "
             "DATE_FORMAT(create_time, '%%Y-%%m-%%d %%H:%%i:%%s'), status "
             "FROM t_patient %s ORDER BY patient_id",
             where ? where : "");

    res = db_query(db, buf);
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Patient*)malloc(sizeof(Patient) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            Patient* p;

            /* 双保险：数组按 res->row_cnt 分配，万一结果集比报出来的行数还多，
             * 宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            p = &arr[n];
            memset(p, 0, sizeof(*p));
            snprintf(p->patient_id,  sizeof(p->patient_id),  "%s", col_str(res, row, 0));
            snprintf(p->name,        sizeof(p->name),        "%s", col_str(res, row, 1));
            snprintf(p->gender,      sizeof(p->gender),      "%s", col_str(res, row, 2));
            p->age = col_int(res, row, 3);
            snprintf(p->id_card,     sizeof(p->id_card),     "%s", col_str(res, row, 4));
            snprintf(p->phone,       sizeof(p->phone),       "%s", col_str(res, row, 5));
            snprintf(p->address,     sizeof(p->address),     "%s", col_str(res, row, 6));
            snprintf(p->username,    sizeof(p->username),    "%s", col_str(res, row, 7));
            snprintf(p->password,    sizeof(p->password),    "%s", col_str(res, row, 8));
            snprintf(p->create_time, sizeof(p->create_time), "%s", col_str(res, row, 9));
            p->status = col_int(res, row, 10);
            n++;
        }
    }
    db_res_free(res);

    /* 一行都没有时返回 NULL，让调用方按"没有记录"处理 */
    if (n == 0) { free(arr); arr = NULL; }

    *out_list  = arr;
    *out_count = n;
    return 0;
}
