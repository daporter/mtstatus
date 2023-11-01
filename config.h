#ifndef CONFIG_H
#define CONFIG_H

#include "components.h"

/* A status bar component */
struct component {
	void (*update)(char *);
	int sleep_secs;
	int signum;
};

#define MAX_COMP_LEN 128
#define NCOMPONENTS  ((sizeof components) / (sizeof(struct component)))

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
static const struct component components[] = {
	/* function, sleep, signal */
	{ ram_free, 2, -1 },
	{ datetime, 30, -1 },
};

#endif
