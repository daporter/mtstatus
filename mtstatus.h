#ifndef MTSTATUS_H
#define MTSTATUS_H

#include <X11/Xlib.h>

#define DIVIDER    "  "
#define NO_VAL_STR "??"
#define ERR_STR    "err"

static Display *dpy;

static void log_err(const char *fmt, ...);

#endif
