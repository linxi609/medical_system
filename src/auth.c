/* ==========================================================================
 * auth.c —— M08 登录与用户 实现
 * --------------------------------------------------------------------------
 * 负责组：三组共用
 * 对应头文件：inc/auth.h
 * 说明：函数声明和返回值含义见 auth.h。
 *       role 取值：1 管理员，2 医生，3 患者。
 *
 * 【实现上的三个注意点】
 *   1) db_fetch_row 返回的是库内部的 static 结构，下一次 fetch 就把它覆盖了，
 *      所以取到字段必须马上拷进本地变量，不能存着指针慢慢用。
 *   2) 拼 SQL 的字符串一律先过 mysql_real_escape_string（本文件的 sql_escape），
 *      否则用户名/密码里带一个单引号就能把语句截断，登录直接失效。
 *   3) db_execute 返回的是 mysql_affected_rows，UPDATE 把某行改成和原值一样时
 *      它同样返回 0，所以不能用 "返回值 == 0" 判断"记录不存在"。
 *      本文件一律先 SELECT 确认记录在，再 UPDATE。
 * ========================================================================== */

#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ==========================================================================
 * 登录失败锁定表
 * ==========================================================================
 * 三张用户表都没有失败次数/锁定时间的字段，也不该为这个改表结构
 * （见 auth.h 的【锁定状态存在哪】），所以在这里用 static 数组记。
 * 单进程运行够用，程序重启计数清零，本项目可以接受。
 *
 * 键取"角色 + 用户名"而不是光用户名：三张表的用户名各自唯一，
 * 不同角色可以用同一个用户名，不能因为患者 P 连续输错就把医生 D 锁了。
 * ========================================================================== */
typedef struct {
    int    role;            /* 角色 1/2/3 */
    char   username[64];    /* 登录名；首字节为 '\0' 表示这个槽是空的 */
    int    fails;           /* 连续失败次数，登录成功或锁定到期后清零 */
    time_t lock_until;      /* 锁定截止时刻；0 表示未锁定 */
} LoginAttempt;

static LoginAttempt s_attempts[32];

/* --------------------------------------------------------------------------
 * 在锁定表里找该用户的记录
 * 参数：create  0 只查；1 查不到就分配一个槽
 * 说明：找到的记录如果锁定已到期，顺手清零，等于恢复成 5 次机会；
 *       表满了优先用空槽，没有空槽就淘汰锁得最早的一条。
 * 返回：记录指针；create=0 且没有该用户时返回 NULL
 * -------------------------------------------------------------------------- */
static LoginAttempt* attempt_slot(int role, const char* username, int create) {
    time_t now = time(NULL);

    for (int i = 0; i < 32; i++) {
        LoginAttempt* a = &s_attempts[i];
        if (a->username[0] != '\0' && a->role == role &&
            strcmp(a->username, username) == 0) {
            if (a->lock_until != 0 && now >= a->lock_until) {
                a->fails      = 0;
                a->lock_until = 0;
            }
            return a;
        }
    }
    if (!create) return NULL;

    LoginAttempt* victim = NULL;
    for (int i = 0; i < 32; i++) {
        LoginAttempt* a = &s_attempts[i];
        if (a->username[0] == '\0') { victim = a; break; }
        /* lock_until 为 0（未锁定）的一定比锁定中的更值得淘汰 */
        if (!victim || a->lock_until < victim->lock_until) victim = a;
    }
    memset(victim, 0, sizeof(*victim));
    victim->role = role;
    snprintf(victim->username, sizeof(victim->username), "%s", username);
    return victim;
}

/* 记一次登录失败。返回给调用方的错误码：未满 5 次是 -4，刚满 5 次锁 10 分钟返回 -7 */
static int record_failure(int role, const char* username) {
    LoginAttempt* a = attempt_slot(role, username, 1);
    a->fails++;
    if (a->fails >= 5) {
        a->lock_until = time(NULL) + 600;
        return -7;
    }
    return -4;
}

/* 登录成功：清掉失败记录，让这个槽空出来 */
static void record_success(int role, const char* username) {
    LoginAttempt* a = attempt_slot(role, username, 0);
    if (a) {
        a->fails      = 0;
        a->lock_until = 0;
        a->username[0] = '\0';
    }
}

/* ==========================================================================
 * SQL 小工具
 * ========================================================================== */

/* 转义后写入 buf，供拼 SQL 使用。
 * 返回 0 成功；-1 参数非法或 buf 装不下（装不下宁可报错，绝不截断后拼进 SQL） */
static int sql_escape(DB_HANDLE* db, const char* in, char* buf, size_t size) {
    if (!db || !db->mysql || !in || !buf) return -1;
    size_t len = strlen(in);
    if (size < len * 2 + 1) return -1;
    mysql_real_escape_string(db->mysql, buf, in, (unsigned long)len);
    return 0;
}

/* 复制查询结果里的一个字段。列为 NULL 时当空串处理，避免 atoi/strcmp 撞空指针 */
static void copy_col(char* dst, size_t size, const char* src) {
    snprintf(dst, size, "%s", src ? src : "");
}

/* 角色 → 用户表名 / 主键列名 */
static const char* user_table(int role) {
    if (role == 1) return "t_admin";
    if (role == 2) return "t_doctor";
    return "t_patient";
}

static const char* user_id_col(int role) {
    if (role == 1) return "admin_id";
    if (role == 2) return "doctor_id";
    return "patient_id";
}

/* --------------------------------------------------------------------------
 * 按用户名在指定角色的表里查一条用户
 * 参数：其余都是出参，调用方给缓冲区，函数负责填
 * 返回：0 找到；1 该表里没有这个用户名；-2 SQL 失败
 * 说明：t_admin 表没有 status 字段（见 init.sql），管理员恒按 status=1 处理
 * -------------------------------------------------------------------------- */
static int fetch_user_by_name(DB_HANDLE* db, int role, const char* username,
                              char* id_out, size_t id_size,
                              char* name_out, size_t name_size,
                              char* pwd_out, size_t pwd_size,
                              int* status_out) {
    char esc[128];
    char sql[512];

    if (sql_escape(db, username, esc, sizeof(esc)) != 0) return -2;

    if (role == 1) {
        snprintf(sql, sizeof(sql),
                 "SELECT admin_id, name, password FROM t_admin WHERE username='%s'",
                 esc);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT %s, name, password, status FROM %s WHERE username='%s'",
                 user_id_col(role), user_table(role), esc);
    }

    DB_RES* res = db_query(db, sql);
    if (!res) return -2;

    DB_ROW* row = db_fetch_row(res);
    if (!row) {                       /* 没有数据行 = 该表里查无此人 */
        db_res_free(res);
        return 1;
    }
    /* 字段必须立刻拷走：db_fetch_row 的返回值是 static 的，下次调用就变 */
    copy_col(id_out,   id_size,   row->fields[0]);
    copy_col(name_out, name_size, row->fields[1]);
    copy_col(pwd_out,  pwd_size,  row->fields[2]);
    if (role == 1) {
        *status_out = 1;
    } else {
        *status_out = row->fields[3] ? atoi(row->fields[3]) : 0;
    }

    db_res_free(res);
    return 0;
}

/* ==========================================================================
 * M08 接口实现
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * 登录
 * 流程：查锁定状态 → 按角色查表 → 校验 status 和密码 → 回填 out
 * 返回：0 成功；-4 用户名或密码错误；-6 该账号不属于所选身份；
 *       -7 账号已锁定或已注销/离职；-2 SQL 执行失败；-1 连接失败
 * -------------------------------------------------------------------------- */
int user_login(DB_HANDLE* db, int role, const char* username,
               const char* password, LoginUser* out) {
    if (!db || !db->is_connected) return -1;
    if (role < 1 || role > 3)     return -6;
    if (!username || !password || !out) return -2;

    /* 1. 锁定检查放最前面：锁定期内不再查库，也不泄露账号是否存在 */
    LoginAttempt* att = attempt_slot(role, username, 0);
    if (att && att->lock_until != 0 && time(NULL) < att->lock_until) return -7;

    /* 2. 按角色查对应的表。id/name 的缓冲区直接对齐 LoginUser 字段宽度，
     *    后面回填时不可能截断（多余字符在源头上就没有） */
    char id[20];
    char name[50];
    char stored[160];
    int  status = 1;

    int ret = fetch_user_by_name(db, role, username,
                                 id, sizeof(id), name, sizeof(name),
                                 stored, sizeof(stored), &status);
    if (ret == -2) return -2;

    if (ret == 1) {
        /* 本表查无此人。用户名如果出现在其它角色的表里，那是选错了身份，
         * 提示 -6 比笼统的"密码错误"有用；两边都没有才按用户名/密码错误算。 */
        for (int other = 1; other <= 3; other++) {
            if (other == role) continue;
            char oid[32], oname[64], opwd[160];
            int  ostatus = 1;
            int r2 = fetch_user_by_name(db, other, username,
                                        oid, sizeof(oid), oname, sizeof(oname),
                                        opwd, sizeof(opwd), &ostatus);
            if (r2 == -2) return -2;
            if (r2 == 0)  return -6;
        }
        /* 用户名不存在也记一次失败：否则可以拿错误用户名无限试，绕过锁定 */
        return record_failure(role, username);
    }

    /* 3. 注销/离职的账号（status != 1）直接拒绝，不计入失败次数 */
    if (status != 1) return -7;

    /* 4. 校验密码 */
    char* hashed = hash_password(password);
    if (!hashed) return -2;
    int pwd_ok = (strcmp(hashed, stored) == 0);
    free(hashed);
    if (!pwd_ok) return record_failure(role, username);

    /* 5. 登录成功：清失败记录，回填用户信息 */
    record_success(role, username);
    snprintf(out->id,   sizeof(out->id),   "%s", id);
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->role = role;
    return 0;
}

/* --------------------------------------------------------------------------
 * 患者注册（菜单入口）
 * 说明：本函数只做"账号、身份证号不重复"的校验和提示，
 *       建档入库由 M01 的 patient_register 完成（编号也由它生成回写）
 * 返回：0 成功；-3 身份证号或账号已存在；-2 SQL 执行失败；-1 连接失败
 * -------------------------------------------------------------------------- */
int user_register_patient(DB_HANDLE* db, Patient* p) {
    if (!db || !db->is_connected) return -1;
    if (!p) return -2;

    char esc_user[128];
    char esc_card[64];
    if (sql_escape(db, p->username, esc_user, sizeof(esc_user)) != 0) return -2;
    if (sql_escape(db, p->id_card,  esc_card,  sizeof(esc_card))  != 0) return -2;

    /* 注销过的账号（status=0）不释放唯一键，所以这里不过滤 status：
     * 还能查到就说明唯一约束会冲突，直接按重复处理 */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT patient_id FROM t_patient "
             "WHERE username='%s' OR id_card='%s' LIMIT 1",
             esc_user, esc_card);

    DB_RES* res = db_query(db, sql);
    if (!res) return -2;
    int dup = (db_fetch_row(res) != NULL);
    db_res_free(res);
    if (dup) return -3;

    p->status = 1;
    return patient_register(db, p);
}

/* --------------------------------------------------------------------------
 * 修改本人密码
 * 返回：0 成功；-4 原密码错误（含查不到本人记录）；-2 SQL 执行失败；-1 连接失败
 * -------------------------------------------------------------------------- */
int user_change_password(DB_HANDLE* db, int role, const char* id,
                         const char* old_pwd, const char* new_pwd) {
    if (!db || !db->is_connected) return -1;
    if (role < 1 || role > 3)     return -6;
    if (!id || !old_pwd || !new_pwd) return -2;

    char esc_id[64];
    char esc_new[224];
    if (sql_escape(db, id,      esc_id,  sizeof(esc_id))  != 0) return -2;
    if (sql_escape(db, new_pwd, esc_new, sizeof(esc_new)) != 0) return -2;

    /* 先取原密码来比对。不能"直接 UPDATE ... WHERE password=原密码"：
     * 那样没法区分"原密码错"和"SQL 出错"，返回值约定就废了 */
    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT password FROM %s WHERE %s='%s'",
             user_table(role), user_id_col(role), esc_id);

    DB_RES* res = db_query(db, sql);
    if (!res) return -2;

    DB_ROW* row = db_fetch_row(res);
    if (!row) {                               /* 查不到本人记录，按原密码错误处理 */
        db_res_free(res);
        return -4;
    }
    char stored[160];
    copy_col(stored, sizeof(stored), row->fields[0]);
    db_res_free(res);

    char* hashed = hash_password(old_pwd);
    if (!hashed) return -2;
    int pwd_ok = (strcmp(hashed, stored) == 0);
    free(hashed);
    if (!pwd_ok) return -4;

    snprintf(sql, sizeof(sql), "UPDATE %s SET password='%s' WHERE %s='%s'",
             user_table(role), esc_new, user_id_col(role), esc_id);
    if (db_execute(db, sql) < 0) return -2;

    /* 这里不能看 affected rows：新旧密码相同时它是 0，但那就是改成功了 */
    return 0;
}

/* --------------------------------------------------------------------------
 * 密码处理（预留接口）
 * 说明：当前教学阶段直接返回明文副本，和 init.sql 里的预置数据一致。
 *       换成加盐 SHA-256 时，本函数和 init.sql 的预置密码要一起改，
 *       并且要在库里存新格式的哈希，否则已有账号全部登不上。
 * 返回：malloc 出的字符串，调用者负责 free
 * -------------------------------------------------------------------------- */
char* hash_password(const char* plain) {
    if (!plain) return NULL;
    char* out = (char*)malloc(strlen(plain) + 1);
    if (!out) return NULL;
    strcpy(out, plain);
    return out;
}
