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

void cli_prompt(const char* label, char* buf, int size) {
    for (;;) {
        printf("%s", label);
        fflush(stdout);
        if (cli_read_line(buf, size) != 0) { buf[0] = '\0'; return; }
        if (utf8_valid(buf)) return;
        printf("输入里含有非 UTF-8 的字节，MySQL 会拒绝这样的内容。常见原因有两个：\n"
               "  1) 当前终端/粘贴内容的编码不是 UTF-8 —— 确认 locale 里带 UTF-8；\n"
               "  2) 这一行太长，被输入缓冲区截断，末尾正好切在汉字中间 —— 请分短一些。\n"
               "请修改后重新输入：\n");
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
    if (sscanf(buf, "%d", out) != 1) return -1;
    return 0;
}

void cli_keep_or(const char* defval, char* buf, int size) {
    if (buf[0] == '\0' && defval && defval[0]) {
        snprintf(buf, (size_t)size, "%s", defval);
    }
}
