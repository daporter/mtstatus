#ifndef CONFIG_H
#define CONFIG_H

#include "components.h"
#include "sbar.h"

#define MAX_COMP_SIZE 128

const char divider[] = "  ";
const char no_val_str[] = "n/a";

/* The components that make up the status bar.

   Each element consists of an updater function and a sleep interval (in
   seconds).  The order of the elements defines the order of components in the
   status bar. */

/*
 * Realtime signals are not individually identified by different constants in
 * the manner of standard signals. However, an application should not hard-code
 * integer values for them, since the range used for realtime signals varies
 * across UNIX implementations. Instead, a realtime signal number can be
 * referred to by adding a value to SIGRTMIN; for example, the expression
 * (SIGRTMIN + 1) refers to the second realtime signal.
 */

/* clang-format off */
 const sbar_cmp_t components[] = {
        /* function, arguments,     sleep, signal */
        { keyb_ind,  NULL,          -1,     0 },
        { notmuch,   NULL,          -1,     1 },
        /* network traffic */
        { load_avg,  NULL,           2,    -1 },
        { ram_free,  NULL,           2,    -1 },
        { disk_free, "/",           15,    -1 },
        /* volume */
        /* wifi */
        { datetime,  "%a %d %b %R", 30,    -1 },
};
/* clang-format on */

#endif
