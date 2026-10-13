/* ==========================================================================
 * doctor.c —— M02 医生信息管理 实现
 * --------------------------------------------------------------------------
 * 负责组：管理员组
 * 对应头文件：inc/doctor.h
 * 对应表：t_doctor
 *
 * 实现说明：
 *   1) 医生编号按 "D + 3 位流水号" 生成：查当前最大流水号加 1。
 *      取最大值时按数字比（CAST ... AS UNSIGNED），不是按字符串 ——
 *      字符串序下 "D1000" 会排在 "D999" 前面，编号过千就会取错。
 *      写法与 patient.c 一致。
 *   2) 离职是软删除（status=0），编号只增不减，绝不重排。
 *      t_appointment / t_visit_record / t_prescription / t_medical_record
 *      四张表都存着 doctor_id，把编号往前挪会让这些历史记录指向别的医生
 *      （库上没建外键，改错了不会报错）。账号（username）也不释放，
 *      和 patient.c 注销的处理一致。
 *   3) 所有外部输入拼进 SQL 前都过 escape_str() 转义。
 *   4) 待组里定：doctor_query_by_id / doctor_update 目前不筛 status，
 *      而 doctor.h 的 -4 写的是"医生不存在或已离职"、menu_admin.c 的提示语
 *      也这么写。管理员到底该不该看到离职医生的档案，定了再统一改。
 * ========================================================================== */

#include "doctor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 主 SQL 缓冲区。最坏情况 7 个文本字段全按 2 倍转义约 611 字节，
 * 加上语句骨架约 150 字节，1024 余量偏紧，取 2048 */
#define DOC_SQL_BUF 2048
/* 最长的文本字段是 password[100]，转义后最多 2*100+3 = 203，取 224 足够
 * （patient.c 里 password 也是取的 224） */
#define DOC_ESC_BUF 224

/* --------------------------------------------------------------------------
 * 本文件用到的小工具（写法与 medicine.c / patient.c 保持一致）
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

/* 结果集的一行 -> Doctor（列顺序与下面两条 SELECT 一致）。
 * 逐个 snprintf 拷走内容，不保存行内指针：db_fetch_row 返回的是 static 结构，
 * db_res_free 之后列指针就悬空了，必须先拷再 free。 */
static void doctor_fill_row(DB_RES* res, DB_ROW* row, Doctor* d) {
    memset(d, 0, sizeof(*d));
    snprintf(d->doctor_id, sizeof(d->doctor_id), "%s", col_str(res, row, 0));
    snprintf(d->name,      sizeof(d->name),      "%s", col_str(res, row, 1));
    snprintf(d->gender,    sizeof(d->gender),    "%s", col_str(res, row, 2));
    snprintf(d->title,     sizeof(d->title),     "%s", col_str(res, row, 3));
    snprintf(d->dept,      sizeof(d->dept),      "%s", col_str(res, row, 4));
    snprintf(d->phone,     sizeof(d->phone),     "%s", col_str(res, row, 5));
    snprintf(d->username,  sizeof(d->username),  "%s", col_str(res, row, 6));
    snprintf(d->password,  sizeof(d->password),  "%s", col_str(res, row, 7));
    d->status = col_int(res, row, 8);
}

/* ==========================================================================
 * 新增医生
 * 说明：登录账号（username）由本函数设成医生编号，调用者传进来的值会被覆盖，
 *       所以菜单层不用再问管理员要账号名
 * 返回：0 成功；-3 登录账号已存在；-2 SQL 执行失败；-1 连接失败
 * ========================================================================== */
int doctor_add(DB_HANDLE* db, Doctor* d) {
    char buf[DOC_SQL_BUF];
    char esc_name[DOC_ESC_BUF];
    char esc_gender[DOC_ESC_BUF];
    char esc_title[DOC_ESC_BUF];
    char esc_dept[DOC_ESC_BUF];
    char esc_phone[DOC_ESC_BUF];
    char esc_username[DOC_ESC_BUF];
    char esc_password[DOC_ESC_BUF];
    char* lit_name;
    char* lit_gender;
    char* lit_title;
    char* lit_dept;
    char* lit_phone;
    char* lit_username;
    char* lit_password;
    DB_RES* res;
    DB_ROW* row;
    char idtmp[16];
    int  idlen;
    int  seq = 0;

    if (!db || !db->is_connected) return -1;
    if (!d) return -2;

    /* ---------- 生成编号：D + 3 位流水号 ----------
     * 按数字取最大流水号，加 1 即为新编号；表里没有医生时 MAX 是 NULL，从 1 开始。
     * 离职的医生仍占着编号（软删除），所以编号只增不减、不复用。 */
    res = db_query(db,
                   "SELECT MAX(CAST(SUBSTRING(doctor_id, 2) AS UNSIGNED)) "
                   "FROM t_doctor");
    if (!res) return -2;

    row = db_fetch_row(res);
    if (row && row->fields[0]) seq = atoi(row->fields[0]);
    db_res_free(res);
    seq++;
    /* doctor_id 是 char[10]："D" + 最多 8 位流水号 + 结尾符正好放下。
     * 先在够大的临时缓冲里格式化，长度超了直接报错 —— 这个上限实际到不了
     * （要一亿个医生），挡一下是为了别把截断的编号插进主键 */
    idlen = snprintf(idtmp, sizeof(idtmp), "D%03d", seq);
    if (idlen < 0 || (size_t)idlen >= sizeof(d->doctor_id)) return -2;
    memcpy(d->doctor_id, idtmp, (size_t)idlen + 1);

    /* 登录账号 = 医生编号：由系统生成，管理员不用（也不该）另起账号名。
     * 编号不会重复，离职后账号继续占着位子也就不是问题了。 */
    snprintf(d->username, sizeof(d->username), "%s", d->doctor_id);

    /* ---------- 转义各文本字段 ----------
     * doctor_id 是自己生成的 "D+数字"，直接拼不用转义 */
    lit_name     = escape_str(db, d->name,     esc_name,     sizeof(esc_name));
    lit_gender   = escape_str(db, d->gender,   esc_gender,   sizeof(esc_gender));
    lit_title    = escape_str(db, d->title,    esc_title,    sizeof(esc_title));
    lit_dept     = escape_str(db, d->dept,     esc_dept,     sizeof(esc_dept));
    lit_phone    = escape_str(db, d->phone,    esc_phone,    sizeof(esc_phone));
    lit_username = escape_str(db, d->username, esc_username, sizeof(esc_username));
    lit_password = escape_str(db, d->password, esc_password, sizeof(esc_password));
    if (!lit_name || !lit_gender || !lit_title || !lit_dept ||
        !lit_phone || !lit_username || !lit_password) return -2;

    /* ---------- 插入 ----------
     * 新医生一律在职（status=1），离职由 doctor_delete 管 */
    snprintf(buf, sizeof(buf),
             "INSERT INTO t_doctor "
             "(doctor_id, name, gender, title, dept, phone, username, password, status) "
             "VALUES ('%s', %s, %s, %s, %s, %s, %s, %s, 1)",
             d->doctor_id, lit_name, lit_gender, lit_title, lit_dept,
             lit_phone, lit_username, lit_password);

    if (db_execute(db, buf) < 0) {
        /* 1062 = 唯一索引冲突：username 撞了按 -3 处理，其余算 SQL 失败 */
        if (mysql_errno(db->mysql) == 1062) return -3;
        return -2;
    }
    return 0;
}

/* ==========================================================================
 * 医生离职（软删除：status 置 0，不删数据、不重排编号）
 * 返回：0 成功；-4 医生不存在或已离职；-2 SQL 执行失败；-1 连接失败
 *
 * 写法与 patient_delete 一致：条件里带 status = 1，已离职的再办一次
 * 影响 0 行，按"不存在"处理。
 * ========================================================================== */
int doctor_delete(DB_HANDLE* db, const char* doctor_id) {
    char buf[512];
    char esc[64];
    int  affected;

    if (!db || !db->is_connected) return -1;
    if (!doctor_id || !doctor_id[0]) return -4;

    if (!escape_str(db, doctor_id, esc, sizeof(esc))) return -2;

    /* 条件里带 status = 1：已离职的再办一次就是 0 行，按"不存在"处理 */
    snprintf(buf, sizeof(buf),
             "UPDATE t_doctor SET status = 0 "
             "WHERE doctor_id = %s AND status = 1", esc);

    affected = db_execute(db, buf);
    if (affected < 0) return -2;
    if (affected == 0) return -4;      /* 不存在，或之前已经离职了 */
    return 0;
}

/* ==========================================================================
 * 修改医生信息
 * 返回：0 成功；-4 医生不存在；-2 SQL 执行失败；-1 连接失败
 *
 * password 传空串表示不修改，用 COALESCE(NULLIF(...)) 保持原值。
 * status 不在这里改，由 doctor_add / doctor_delete 管。
 * 注意：这里不筛 status（离职的也能改），见文件头说明 4)
 * ========================================================================== */
int doctor_update(DB_HANDLE* db, const Doctor* d) {
    char buf[DOC_SQL_BUF];
    char esc_id[64];
    char esc_name[DOC_ESC_BUF];
    char esc_gender[DOC_ESC_BUF];
    char esc_title[DOC_ESC_BUF];
    char esc_dept[DOC_ESC_BUF];
    char esc_phone[DOC_ESC_BUF];
    char esc_password[DOC_ESC_BUF];
    char* lit_id;
    char* lit_name;
    char* lit_gender;
    char* lit_title;
    char* lit_dept;
    char* lit_phone;
    char* lit_password;
    DB_RES* res;
    DB_ROW* row;
    int exists;

    if (!db || !db->is_connected) return -1;
    if (!d || !d->doctor_id[0]) return -4;

    /* ---------- 先确认这个医生存在，不存在返回 -4 ----------
     * 只在这一行上判有无，不引用行里的字段，所以可以先 free 再判断 */
    lit_id = escape_str(db, d->doctor_id, esc_id, sizeof(esc_id));
    if (!lit_id) return -2;

    snprintf(buf, sizeof(buf), "SELECT doctor_id FROM t_doctor WHERE doctor_id = %s", lit_id);
    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    exists = (row != NULL);
    db_res_free(res);
    if (!exists) return -4;

    /* ---------- 转义改后的各字段 ---------- */
    lit_name     = escape_str(db, d->name,     esc_name,     sizeof(esc_name));
    lit_gender   = escape_str(db, d->gender,   esc_gender,   sizeof(esc_gender));
    lit_title    = escape_str(db, d->title,    esc_title,    sizeof(esc_title));
    lit_dept     = escape_str(db, d->dept,     esc_dept,     sizeof(esc_dept));
    lit_phone    = escape_str(db, d->phone,    esc_phone,    sizeof(esc_phone));
    lit_password = escape_str(db, d->password, esc_password, sizeof(esc_password));
    if (!lit_name || !lit_gender || !lit_title ||
        !lit_dept || !lit_phone || !lit_password) return -2;

    /* ---------- 更新 ----------
     * password 传空串时 NULLIF 把它变成 NULL，COALESCE 再取回原值，等于不改 */
    snprintf(buf, sizeof(buf),
             "UPDATE t_doctor SET name = %s, gender = %s, title = %s, "
             "dept = %s, phone = %s, "
             "password = COALESCE(NULLIF(%s, ''), password) "
             "WHERE doctor_id = %s",
             lit_name, lit_gender, lit_title, lit_dept, lit_phone,
             lit_password, lit_id);

    if (db_execute(db, buf) < 0) return -2;
    return 0;
}

/* ==========================================================================
 * 按编号查询医生
 * 返回：0 成功；-4 未找到；-2 SQL 执行失败；-1 连接失败
 * ========================================================================== */
int doctor_query_by_id(DB_HANDLE* db, const char* doctor_id, Doctor* out) {
    char buf[512];
    char esc[64];
    DB_RES* res;
    DB_ROW* row;

    if (!db || !db->is_connected) return -1;
    if (!doctor_id || !doctor_id[0] || !out) return -4;

    if (!escape_str(db, doctor_id, esc, sizeof(esc))) return -2;

    snprintf(buf, sizeof(buf),
             "SELECT doctor_id, name, gender, title, dept, phone, username, "
             "password, status "
             "FROM t_doctor WHERE doctor_id = %s", esc);

    res = db_query(db, buf);
    if (!res) return -2;

    row = db_fetch_row(res);
    if (!row) {
        db_res_free(res);
        return -4;
    }

    doctor_fill_row(res, row, out);   /* 先拷走内容，再释放结果集 */
    db_res_free(res);
    return 0;
}

/* ==========================================================================
 * 查询医生列表（患者挂号时用，只列在职的）
 * 返回：0 成功（可能 out_count 为 0）；-2 SQL 执行失败；-1 连接失败
 * ========================================================================== */
int doctor_query_all(DB_HANDLE* db, Doctor** out_list, int* out_count) {
    DB_RES*  res;
    DB_ROW*  row;
    Doctor*  arr = NULL;
    int      n = 0;

    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !db->is_connected) return -1;
    if (!out_list || !out_count) return -2;

    res = db_query(db,
                   "SELECT doctor_id, name, gender, title, dept, phone, "
                   "username, password, status "
                   "FROM t_doctor WHERE status = 1 ORDER BY doctor_id");
    if (!res) return -2;

    if (res->row_cnt > 0) {
        arr = (Doctor*)malloc(sizeof(Doctor) * (size_t)res->row_cnt);
        if (!arr) { db_res_free(res); return -2; }

        while ((row = db_fetch_row(res)) != NULL) {
            /* 双保险：数组按 res->row_cnt 分配，理论上取不到这么多行；
             * 万一结果集比报出来的行数还多，宁可少读也不能写越界 */
            if (n >= res->row_cnt) break;

            doctor_fill_row(res, row, &arr[n]);
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

/* ==========================================================================
 * 释放 doctor_query_all 返回的数组
 * ========================================================================== */
void doctor_list_free(Doctor* list) {
    free(list);
}
