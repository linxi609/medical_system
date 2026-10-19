#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

void str_trim(char *s);
int  is_all_digit(const char *s);
void copy_str(char* dst, size_t dstsz, const char* src);


int  utf8_width(const char *s);

int  utf8_valid(const char *s);

void table_row(const char *cells[], const int widths[], int n);

#endif
