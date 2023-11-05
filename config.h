#ifndef CONFIG_H
#define CONFIG_H

#include "component.h"
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
        /* function,            arguments,      sleep,  signal */
        { component_keyb_ind,   NULL,           -1,      0 },
        { component_notmuch,    NULL,           -1,      1 },
        /* network traffic */
        { component_load_avg,   NULL,            2,     -1 },
        { component_ram_free,   NULL,            2,     -1 },
        { component_disk_free,  "/",            15,     -1 },
        /* volume */
        /* wifi */
        { component_datetime,   "%a %d %b %R",  30,     -1 },
};
/* clang-format on */

#endif
