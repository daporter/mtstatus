#ifndef ERRORS_H
#define ERRORS_H

#include <stdarg.h>

/* Error diagnostic routines */

void warn_errnum(int errnum, const char *format, ...);
void warn(const char *format, ...);
void die_errnum(int errnum, const char *format, ...);
void die_errno(const char *fmt, ...);
void die(const char *format, ...);

#endif
