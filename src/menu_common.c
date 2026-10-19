#include "menu_common.h"
#include "cli_utils.h"

#include <stdio.h>
#include <string.h>

const char* err_text(int ret) {
    switch (ret) {
        case -1: return "数据库连接失败";
        case -2: return "SQL 执行失败";
        case -3: return "唯一字段重复";
        case -4: return "记录不存在";
        case -5: return "库存不足";
        case -6: return "越权操作或状态不允许";
        case -7: return "账号或病历已锁定";
        case -8: return "重复挂号";
        default: return "未知错误";
    }
}

const char* appt_status_text(int status) {
    switch (status) {
        case 0: return "待就诊";
        case 1: return "已就诊";
        case 2: return "已取消";
        default: return "未知";
    }
}

const char* presc_status_text(int status) {
    return status == 1 ? "有效" : "已作废";
}

void menu_change_password(DB_HANDLE* db, const LoginUser* user) {
    char old_pwd[64];
    char new_pwd[64];
    char confirm[64];
    int ret;

    cli_prompt("原密码：",     old_pwd, sizeof(old_pwd));
    cli_prompt("新密码：",     new_pwd, sizeof(new_pwd));
    cli_prompt("确认新密码：", confirm, sizeof(confirm));

    if (new_pwd[0] == '\0') {
        printf("新密码不能为空，已取消。\n");
        return;
    }
    if (strcmp(new_pwd, confirm) != 0) {
        printf("两次输入的新密码不一致，已取消。\n");
        return;
    }
    if (strcmp(old_pwd, new_pwd) == 0) {
        printf("新密码和原密码相同，已取消。\n");
        return;
    }

    ret = user_change_password(db, user->role, user->id, old_pwd, new_pwd);
    if (ret == 0) {
        printf("密码修改成功，下次登录请用新密码。\n");
    } else if (ret == -4) {
        printf("修改失败：原密码错误\n");
    } else {
        printf("修改失败：%s\n", err_text(ret));
    }
}
