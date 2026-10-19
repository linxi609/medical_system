#ifndef CLI_UTILS_H
#define CLI_UTILS_H

int cli_read_line(char* buf, int size);
void cli_prompt(const char* label, char* buf, int size);
int  cli_prompt_chinese(const char* label, char* buf, int size, int allow_empty);
int  cli_prompt_digits(const char* label, char* buf, int size, int allow_empty);
void cli_prompt_int(const char* label, int* out);
void cli_prompt_double(const char* label, double* out);
int cli_read_choice(int* out);
void cli_keep_or(const char* defval, char* buf, int size);

#endif
