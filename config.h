#ifndef CONFIG_H
#define CONFIG_H

typedef void (*updater_t)(char *, const int, const char *);

/* A status bar component */
struct component {
	const updater_t update;
	const char *args;
	const int sleep_secs;
	const int signum;
};

#define MAX_COMP_LEN 128
#define NCOMPONENTS  ((sizeof components) / (sizeof(struct component)))

static const char divider[] = "  ";
static const char no_val_str[] = "n/a";

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
	/* function, arguments, sleep, signal */
	/* keyboard indicators */
	{ notmuch, NULL, -1, 0 },
	/* network traffic */
	{ load_avg, NULL, 2, -1 },
	{ ram_free, NULL, 2, -1 },
	{ disk_free, "/", 15, -1 },
	/* volume */
	/* wifi */
	{ datetime, "%a %d %b %R", 30, -1 },

};

#endif
