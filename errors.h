#ifndef ERRORS_H
#define ERRORS_H

#include <stdarg.h>

/* Error diagnostic routines */

void err_msg_en(int errnum, const char *format, ...);
void err_msg(const char *format, ...);
void err_exit_en(int errnum, const char *format, ...);
void err_exit(const char *format, ...);

#endif
