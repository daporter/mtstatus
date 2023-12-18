#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LEN(x) (sizeof(x) / sizeof((x)[0]))

#define K_SI  1000
#define K_IEC 1024

char *util_cat(char *dest, const char *end, const char *str);
int   util_fmt_human(char *buf, size_t len, uintmax_t num, int base);
bool  util_run_cmd(char *buf, size_t bufsize, char *const argv[]);

#endif
