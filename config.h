#ifndef CONFIG_H
#define CONFIG_H

#include "src/component.h"

#include <time.h>

/* The components that make up the status bar.

   Each element consists of an updater function and sleep interval (in
   seconds).  The order of the elements defines the order of components in the
   status bar. */

/*
 * Function that returns an updated value for a status bar component.
 */
typedef comp_ret_t (*sbar_updater_t)(char *buf, const size_t bufsize,
				     const char *args, const char *no_val_str);

typedef struct {
	const sbar_updater_t update;
	const char *args;
	const time_t interval;
	const int signum;
} sbar_comp_defn_t;

/*
 * Realtime signals are not individually identified by different constants in
 * the manner of standard signals. However, an application should not hard-code
 * integer values for them, since the range used for realtime signals varies
 * across UNIX implementations. Instead, a realtime signal number can be
 * referred to by adding a value to SIGRTMIN; for example, the expression
 * (SIGRTMIN + 1) refers to the second realtime signal.
 */

/* clang-format off */
const sbar_comp_defn_t component_defns[] = {
	/* function,           arguments,     interval, signal (SIGRTMIN+n) */
	{ component_keyb_ind,  NULL,          -1,        0 },
	{ component_notmuch,   NULL,          -1,        1 },
	/* network traffic */
	{ component_mem_avail, NULL,           2,       -1 },
	{ component_disk_free, "/",           15,       -1 },
	/* volume */
	/* wifi */
	{ component_datetime,  "%a %d %b %R", 30,       -1 },
};
/* clang-format on */

#endif
