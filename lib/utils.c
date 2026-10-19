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

int is_all_chinese(const char *s) {
    int i = 0;

    if (s == NULL || s[0] == '\0') return 0;

    while (s[i] != '\0') {
        unsigned char c = (unsigned char)s[i];
        unsigned int  cp;
        int           len;
        int           j;

        if (c < 0x80) return 0;
        if      ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return 0;

        for (j = 1; j < len; j++) {
            unsigned char t = (unsigned char)s[i + j];
            if (t == '\0')            return 0;
            if ((t & 0xC0) != 0x80)   return 0;
            cp = (cp << 6) | (t & 0x3F);
        }

        if (!((cp >= 0x4E00 && cp <= 0x9FFF) ||
              (cp >= 0x3400 && cp <= 0x4DBF) ||
              (cp >= 0xF900 && cp <= 0xFAFF) ||
              (cp >= 0x20000 && cp <= 0x3FFFD))) {
            return 0;
        }

        i += len;
    }
    return 1;
}

void copy_str(char* dst, size_t dstsz, const char* src) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, dstsz, "%s", src);
}

static int utf8_next(const char *s, int *width) {
    unsigned char c = (unsigned char)s[0];
    unsigned int  cp;
    int           len;
    int           i;

    if (c < 0x80) {
        *width = 1;
        return 1;
    }

    if ((c & 0xE0) == 0xC0)      { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else                         { *width = 1; return 1; }

    for (i = 1; i < len; i++) {
        unsigned char t = (unsigned char)s[i];
        if ((t & 0xC0) != 0x80) {
            *width = 1;
            return 1;
        }
        cp = (cp << 6) | (t & 0x3F);
    }

    if ((cp >= 0x1100 && cp <= 0x115F) ||
        cp == 0x2329 || cp == 0x232A ||
        (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x20000 && cp <= 0x3FFFD)) {
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
        unsigned int  min_cp;
        int           len;
        int           j;

        if (c < 0x80) continue;

        if      ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; min_cp = 0x80; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min_cp = 0x800; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; min_cp = 0x10000; }
        else return 0;

        for (j = 1; j < len; j++) {
            unsigned char t = (unsigned char)s[i + j];
            if (t == '\0')            return 0;
            if ((t & 0xC0) != 0x80)   return 0;
            cp = (cp << 6) | (t & 0x3F);
        }

        if (cp < min_cp)                          return 0;
        if (cp > 0x10FFFF)                        return 0;
        if (cp >= 0xD800 && cp <= 0xDFFF)         return 0;

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

        if (i == n - 1) {
            printf("%s\n", s);
            break;
        }

        if (widths[i] < 0) {
            for (int k = -widths[i] - used; k > 0; k--) printf(" ");
            printf("%s", s);
        } else {
            printf("%s", s);
            for (int k = widths[i] - used; k > 0; k--) printf(" ");
        }
        printf(" ");
    }
}
