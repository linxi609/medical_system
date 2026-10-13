/* ==========================================================================
 * main.c —— 程序入口、主菜单、登录流程与角色分发
 * --------------------------------------------------------------------------
 * 负责组：仓库维护者
 * 说明：这是整个项目唯一的 main()。只做四件事：
 *         1. 连数据库（连接参数从环境变量读，见 README）
 *         2. 显示主菜单：1 登录 / 2 患者注册 / 0 退出
 *         3. 登录：选身份 → 输账号密码 → user_login → 按角色跳到对应菜单
 *         4. 患者注册：采集资料 → user_register_patient
 *       三个角色的菜单在 menu_*.c 里，由各自的组实现，本文件只负责调用。
 *
 * 改动提醒：登录流程和分发属于公共部分，各组不要改这里。需要调整请在群里说。
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db_mysql.h"
#include "auth.h"
#include "patient.h"
#include "menu.h"
#include "utils.h"

/* ==========================================================================
 * 输入辅助函数
 * --------------------------------------------------------------------------
 * 全部用 fgets 读整行再解析，不用 scanf 直接读数字。
 * 原因：scanf("%d") 遇到字母会卡住不动，且残留字符会窜到下一次输入里。
 * ========================================================================== */

/* 读一整行，去掉末尾换行。返回 0 成功，-1 遇到文件结束/读失败
 *
 * 注意 else 分支不能省：fgets 最多只读 size-1 个字节，如果这一行比缓冲区还长，
 * 它会刚好把缓冲区填满、连换行都没读到就返回。此时剩下的字符仍留在输入里，
 * 会被下一次读取当成新的一行，把后面每个字段都顶错一位，而且全程不报错。
 * 所以必须把本行剩余的字符吃掉。 */
static int read_line(char* buf, int size) {
    if (!fgets(buf, size, stdin)) return -1;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';            /* 正常情况：读到换行，去掉它 */
    } else {
        int c;                          /* 缓冲区装满了，丢掉本行剩下的字符 */
        while ((c = getchar()) != '\n' && c != EOF) { /* 空循环 */ }
    }
    return 0;
}

/* 显示提示语后读一整行字符串。
 * 读到不是合法 UTF-8 的字节就重问：这种输入原样发给 MySQL 会被拒
 * （Incorrect string value），在这里拦下来才能告诉用户真正的原因。 */
static void prompt(const char* label, char* buf, int size) {
    for (;;) {
        printf("%s", label);
        fflush(stdout);
        if (read_line(buf, size) != 0) { buf[0] = '\0'; return; }
        if (utf8_valid(buf)) return;
        printf("输入里含有非 UTF-8 的字节，MySQL 会拒绝这样的内容。常见原因有两个：\n"
               "  1) 当前终端/粘贴内容的编码不是 UTF-8 —— 确认 locale 里带 UTF-8；\n"
               "  2) 这一行太长，被输入缓冲区截断，末尾正好切在汉字中间 —— 请分短一些。\n"
               "请修改后重新输入：\n");
    }
}

/* 显示提示语后读一个整数 */
static void prompt_int(const char* label, int* out) {
    char buf[32];
    prompt(label, buf, sizeof(buf));
    *out = atoi(buf);
}

/* 读一个菜单选项。返回 0 成功，-1 输入非法，-2 输入结束（按了 Ctrl+D 或管道读完） */
static int read_choice(int* out) {
    char buf[32];
    if (read_line(buf, sizeof(buf)) != 0) return -2;
    if (sscanf(buf, "%d", out) != 1) return -1;
    return 0;
}

/* ==========================================================================
 * 主菜单
 * ========================================================================== */

static void show_main_menu(void) {
    printf("\n========================================\n");
    printf("   社区医疗服务中心信息管理系统 v1.0\n");
    printf("========================================\n");
    printf("  1. 登录\n");
    printf("  2. 患者注册\n");
    printf("  0. 退出\n");
    printf("请输入选项[0-2]: ");
    fflush(stdout);
}

/* ==========================================================================
 * 登录：选身份 → 输账号密码 → 校验 → 按角色跳到对应菜单
 * ========================================================================== */

static void do_login(DB_HANDLE* db) {
    printf("\n请选择身份：\n");
    printf("  1. admin     管理员\n");
    printf("  2. doctor    医生\n");
    printf("  3. patient   患者\n");
    printf("  0. 返回\n");

    int role;
    printf("请输入选项[0-3]: ");
    fflush(stdout);
    int r = read_choice(&role);
    if (r == -2) return;
    if (r != 0 || role < 0 || role > 3) {
        printf("输入无效\n");
        return;
    }
    if (role == 0) return;

    /* 账号由系统生成，不是自己起的用户名，这里按身份给一句提示 */
    if (role == 1)      printf("（管理员账号：admin）\n");
    else if (role == 2) printf("（医生账号：D 开头的医生编号，如 D001）\n");
    else                printf("（患者账号：P 开头的患者编号，如 P20260001）\n");

    char username[50];
    char password[100];
    prompt("请输入账号：", username, sizeof(username));
    prompt("请输入密  码：", password, sizeof(password));

    LoginUser user;
    memset(&user, 0, sizeof(user));

    int ret = user_login(db, role, username, password, &user);
    switch (ret) {
        case 0:
            printf("\n登录成功，欢迎 %s\n", user.name);
            switch (user.role) {
                case 1: menu_admin(db, &user);   break;   /* 管理员主菜单 */
                case 2: menu_doctor(db, &user);  break;   /* 医生工作台   */
                case 3: menu_patient(db, &user); break;   /* 患者工作台   */
                default:
                    /* user_login 还没实现完时会走到这里（role 是 0） */
                    printf("未能识别用户身份，返回主菜单\n");
                    break;
            }
            break;
        case -4: printf("\n账号或密码错误\n"); break;
        case -6: printf("\n该账号不属于所选身份\n"); break;
        case -7: printf("\n账号已锁定，或已注销/离职\n"); break;
        default:
            printf("\n登录失败（错误码 %d）\n", ret);
            break;
    }
}

/* ==========================================================================
 * 患者注册：采集资料 → 建档
 * ========================================================================== */

static void do_register(DB_HANDLE* db) {
    Patient p;
    memset(&p, 0, sizeof(p));
    p.status = 1;

    printf("\n=== 患者注册 ===\n");
    prompt("姓名：",     p.name,     sizeof(p.name));
    prompt("性别：",     p.gender,   sizeof(p.gender));
    prompt_int("年龄：", &p.age);
    prompt("身份证号：", p.id_card,  sizeof(p.id_card));
    prompt("电话：",     p.phone,    sizeof(p.phone));
    prompt("住址：",     p.address,  sizeof(p.address));
    prompt("密码：",     p.password, sizeof(p.password));

    int ret = user_register_patient(db, &p);
    if (ret == 0) {
        printf("\n注册成功！\n");
        printf("  患者编号：%s（登录账号）\n", p.patient_id);
    } else if (ret == -3) {
        printf("\n注册失败：这个身份证号已经注册过了\n");
    } else {
        printf("\n注册失败（错误码 %d）\n", ret);
    }
}

/* ==========================================================================
 * 程序入口
 * ========================================================================== */

/* 读取环境变量，未设置或为空时用默认值 */
static const char* env_or(const char* key, const char* fallback) {
    const char* val = getenv(key);
    return (val && *val) ? val : fallback;
}

int main(void) {
    /* 数据库连接参数从环境变量读取，不要把密码写进源码 */
    const char* db_host = env_or("DB_HOST", "localhost");
    const char* db_user = env_or("DB_USER", "root");
    const char* db_pass = env_or("DB_PASSWORD", "");
    const char* db_name = env_or("DB_NAME", "medical_system");
    int         db_port = atoi(env_or("DB_PORT", "3306"));

    if (!*db_pass) {
        printf("提示: 未设置 DB_PASSWORD 环境变量\n");
    }

    DB_HANDLE* db = db_create();
    if (!db) {
        printf("db_create failed\n");
        return 1;
    }
    if (db_connect(db, db_host, db_user, db_pass, db_name, db_port) != 0) {
        db_close(db);
        return 1;
    }

    int running = 1;
    while (running) {
        show_main_menu();

        int choice;
        int r = read_choice(&choice);
        if (r == -2) break;                 /* 输入结束，直接退出，避免空转 */
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: do_login(db);    break;
            case 2: do_register(db); break;
            case 0: running = 0;     break;
            default: printf("无效选项\n"); break;
        }
    }

    db_close(db);
    printf("程序退出\n");
    return 0;
}
