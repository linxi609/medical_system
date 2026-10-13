#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

void str_trim(char *s) {
    if (s == NULL || s[0] == '\0') return;
    int start = 0;
    while (s[start] != '\0' && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
    int len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

int is_all_digit(const char *s) {
    if (s == NULL || s[0] == '\0') return 0;
    for (int i = 0; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i])) return 0;
    }
    return 1;
}

/* 从 s 的当前位置解出一个 UTF-8 字符，*width 回填它占几个显示列，
 * 返回这个字符占几个字节。非法字节按 1 字节、1 列处理（不崩、不卡死）。 */
static int utf8_next(const char *s, int *width) {
    unsigned char c = (unsigned char)s[0];
    unsigned int  cp;
    int           len;
    int           i;

    if (c < 0x80) {                       /* ASCII：1 字节 1 列 */
        *width = 1;
        return 1;
    }

    /* 多字节序列：首字节 110xxxxx=2 字节，1110xxxx=3 字节，11110xxx=4 字节 */
    if ((c & 0xE0) == 0xC0)      { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else                         { *width = 1; return 1; }   /* 非法首字节 */

    for (i = 1; i < len; i++) {
        unsigned char t = (unsigned char)s[i];
        if ((t & 0xC0) != 0x80) {         /* 后继字节不是 10xxxxxx，非法 */
            *width = 1;
            return 1;
        }
        cp = (cp << 6) | (t & 0x3F);
    }

    /* 宽字符判定（同 wcwidth 的常用区间）：汉字、全角标点、假名、谚文等占 2 列 */
    if ((cp >= 0x1100 && cp <= 0x115F) ||          /* 谚文字母 */
        cp == 0x2329 || cp == 0x232A ||            /* 〈 〉 */
        (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) || /* 汉字/假名/注音等 */
        (cp >= 0xAC00 && cp <= 0xD7A3) ||          /* 谚文音节 */
        (cp >= 0xF900 && cp <= 0xFAFF) ||          /* 兼容汉字 */
        (cp >= 0xFE30 && cp <= 0xFE6F) ||          /* 兼容标点 */
        (cp >= 0xFF00 && cp <= 0xFF60) ||          /* 全角字符 */
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||          /* 全角币符 */
        (cp >= 0x20000 && cp <= 0x3FFFD)) {        /* 汉字扩展 B 区及以后 */
        *width = 2;
    } else {
        *width = 1;
    }
    return len;
}

int utf8_width(const char *s) {
    int total = 0;

    if (s == NULL) return 0;
    while (*s != '\0') {
        int w;
        s += utf8_next(s, &w);
        total += w;
    }
    return total;
}

int utf8_valid(const char *s) {
    if (s == NULL) return 1;
    for (int i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        unsigned int  cp;
        unsigned int  min_cp;             /* 该长度的最短合法码点，用来卡"过长编码" */
        int           len;
        int           j;

        if (c < 0x80) continue;

        if      ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; min_cp = 0x80; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min_cp = 0x800; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; min_cp = 0x10000; }
        else return 0;                    /* 非法首字节（含 0x80-0xBF 孤立后继字节） */

        for (j = 1; j < len; j++) {
            unsigned char t = (unsigned char)s[i + j];
            if (t == '\0')            return 0;   /* 字符串在半截上结束 */
            if ((t & 0xC0) != 0x80)   return 0;   /* 后继字节不是 10xxxxxx */
            cp = (cp << 6) | (t & 0x3F);
        }

        /* MySQL 的 utf8mb4 只收这些范围内的码点，客户端先拦下来，
         * 免得报出来的是看不懂的 "Incorrect string value" */
        if (cp < min_cp)                          return 0;   /* 过长编码 */
        if (cp > 0x10FFFF)                        return 0;   /* 超出 Unicode */
        if (cp >= 0xD800 && cp <= 0xDFFF)         return 0;   /* 代理对，不是合法字符 */

        i += len - 1;
    }
    return 1;
}

void table_row(const char *cells[], const int widths[], int n) {
    if (cells == NULL || widths == NULL || n <= 0) return;

    printf("  ");
    for (int i = 0; i < n; i++) {
        const char *s = cells[i] ? cells[i] : "";
        int used = utf8_width(s);

        if (i == n - 1) {                 /* 最后一列直接打印，不补空格 */
            printf("%s\n", s);
            break;
        }

        if (widths[i] < 0) {              /* 宽度为负 = 右对齐（数字、金额列） */
            for (int k = -widths[i] - used; k > 0; k--) printf(" ");
            printf("%s", s);
        } else {
            printf("%s", s);
            for (int k = widths[i] - used; k > 0; k--) printf(" ");
        }
        printf(" ");                      /* 列之间的一个空格 */
    }
}
