#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli_utils.h"
#include "utils.h"

int cli_read_line(char* buf, int size) {
    if (!fgets(buf, size, stdin)) return -1;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    } else {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {   }
    }
    return 0;
}

static int read_field(const char* label, char* buf, int size) {
    for (;;) {
        printf("%s", label);
        fflush(stdout);
        if (cli_read_line(buf, size) != 0) { buf[0] = '\0'; return -1; }
        if (utf8_valid(buf)) return 0;
        printf("输入里含有非 UTF-8 的字节，MySQL 会拒绝这样的内容。常见原因有两个：\n"
               "  1) 当前终端/粘贴内容的编码不是 UTF-8 —— 确认 locale 里带 UTF-8；\n"
               "  2) 这一行太长，被输入缓冲区截断，末尾正好切在汉字中间 —— 请分短一些。\n"
               "请修改后重新输入：\n");
    }
}

void cli_prompt(const char* label, char* buf, int size) {
    (void)read_field(label, buf, size);
}

int cli_prompt_chinese(const char* label, char* buf, int size, int allow_empty) {
    for (;;) {
        if (read_field(label, buf, size) != 0) return -1;
        if (buf[0] == '\0') {
            if (allow_empty) return 0;
            printf("此项不能为空，请重新输入。\n");
            continue;
        }
        if (is_all_chinese(buf)) return 0;
        printf("此项只能填写汉字（不能有字母、数字、空格或标点），请重新输入。\n");
    }
}

int cli_prompt_digits(const char* label, char* buf, int size, int allow_empty) {
    for (;;) {
        if (read_field(label, buf, size) != 0) return -1;
        if (buf[0] == '\0') {
            if (allow_empty) return 0;
            printf("此项不能为空，请重新输入。\n");
            continue;
        }
        if (is_all_digit(buf)) return 0;
        printf("此项只能填写数字（不能有空格、横线或字母），请重新输入。\n");
    }
}

void cli_prompt_int(const char* label, int* out) {
    char buf[32];
    cli_prompt(label, buf, sizeof(buf));
    *out = atoi(buf);
}

void cli_prompt_double(const char* label, double* out) {
    char buf[32];
    cli_prompt(label, buf, sizeof(buf));
    *out = atof(buf);
}

int cli_read_choice(int* out) {
    char buf[32];
    if (cli_read_line(buf, sizeof(buf)) != 0) return -2;
    str_trim(buf);
    if (!is_all_digit(buf) || strlen(buf) > 4) return -1;
    *out = atoi(buf);
    return 0;
}

void cli_keep_or(const char* defval, char* buf, int size) {
    if (buf[0] == '\0' && defval && defval[0]) {
        snprintf(buf, (size_t)size, "%s", defval);
    }
}
