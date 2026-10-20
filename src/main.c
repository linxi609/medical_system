
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db_mysql.h"
#include "auth.h"
#include "patient.h"
#include "menu.h"
#include "utils.h"
#include "cli_utils.h"











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


static void do_login(DB_HANDLE* db) {
    printf("\n请选择身份：\n");
    printf("  1. admin     管理员\n");
    printf("  2. doctor    医生\n");
    printf("  3. patient   患者\n");
    printf("  0. 返回\n");

    int role;
    printf("请输入选项[0-3]: ");
    fflush(stdout);
    int r = cli_read_choice(&role);
    if (r == -2) return;
    if (r != 0 || role < 0 || role > 3) {
        printf("输入无效\n");
        return;
    }
    if (role == 0) return;

    if (role == 1)      printf("（管理员账号：admin）\n");
    else if (role == 2) printf("（医生账号：D 开头的医生编号，如 D001）\n");
    else                printf("（患者账号：P 开头的患者编号，如 P20260001）\n");

    char username[50];
    char password[100];
    cli_prompt("请输入账号：", username, sizeof(username));
    cli_prompt("请输入密码：", password, sizeof(password));

    LoginUser user;
    memset(&user, 0, sizeof(user));

    int ret = user_login(db, role, username, password, &user);
    switch (ret) {
        case 0:
            printf("\n登录成功，欢迎 %s\n", user.name);
            switch (user.role) {
                case 1: menu_admin(db, &user);   break;
                case 2: menu_doctor(db, &user);  break;
                case 3: menu_patient(db, &user); break;
                default:
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


static void do_register(DB_HANDLE* db) {
    Patient p;
    memset(&p, 0, sizeof(p));
    p.status = 1;

    printf("\n=== 患者注册 ===\n");
    cli_prompt("姓名：",     p.name,     sizeof(p.name));
    cli_prompt("性别：",     p.gender,   sizeof(p.gender));
    cli_prompt_int("年龄：", &p.age);
    cli_prompt("身份证号：", p.id_card,  sizeof(p.id_card));
    cli_prompt("电话：",     p.phone,    sizeof(p.phone));
    cli_prompt("住址：",     p.address,  sizeof(p.address));
    cli_prompt("密码：",     p.password, sizeof(p.password));

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


static const char* env_or(const char* key, const char* fallback) {
    const char* val = getenv(key);
    return (val && *val) ? val : fallback;
}

int main(void) {
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
        int r = cli_read_choice(&choice);
        if (r == -2) break;
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
