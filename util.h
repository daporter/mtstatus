#include <stddef.h>
#include <stdint.h>

#define LEN(x) (sizeof(x) / sizeof((x)[0]))

#define K_SI  1000
#define K_IEC 1024

static char *util_cat(char *dest, const char *end, const char *str);
static int   util_fmt_human(char *buf, size_t len, uintmax_t num, int base);
static int   util_run_cmd(char *buf, size_t bufsize, char *const argv[]);
