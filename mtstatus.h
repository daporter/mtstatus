#ifndef MTSTATUS_H
#define MTSTATUS_H

#include <X11/Xlib.h>

#define DIVIDER    "  "
#define NO_VAL_STR "??"
#define ERR_STR    "err"

Display *dpy;

void log_err(const char *fmt, ...);
void log_errno(int errnum, const char *fmt, ...);

#endif
