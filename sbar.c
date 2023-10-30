/* TODO:
 * - Add more component functions (see ‘syscalls(2) manpage’).
 * - Use config.h.
 * - Add debugging output.
 * - Refactor duplicated code.
 */

#include <X11/Xlib.h>
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"
#include "util.h"

static void ram_free(char *buf);
static void datetime(char *buf);

/* A status bar component */
struct component {
	void (*update)(char *);
	int sleep_secs;
	int signo;
};

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

/* Argument passed to the print-status thread */
struct targ_status {
	bool to_stdout;
	Display *dpy;
};

/* Argument passed to a single-update thread */
struct targ_updater {
	struct component component;
	unsigned posn;
};

#define NCOMPONENTS  ((sizeof components) / (sizeof(struct component)))
#define MAX_COMP_LEN 128

#define UNUSED(x) UNUSED_##x __attribute__((__unused__))

/* Each thread writes to its own output buffer */
static char component_bufs[NCOMPONENTS][MAX_COMP_LEN];
static pthread_mutex_t bufs_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool is_updated = false;
static pthread_cond_t is_updated_cond = PTHREAD_COND_INITIALIZER;

static const char no_val_str[] = "n/a";
static_assert(sizeof(no_val_str) <= sizeof(component_bufs[0]),
	      "no_val_str must be no bigger than component_buf");

static const char divider[] = "  |  ";
static_assert(sizeof(divider) <= sizeof(component_bufs[0]),
	      "divider must be no bigger than component_buf");

static volatile sig_atomic_t done;

/*
 * Array of flags indicating which signals have been received and not yet
   processed.  Set by the signal handler.
 */
static volatile sig_atomic_t *sigs_recv;

static void datetime(char *buf)
{
	time_t t;
	struct tm now;

	t = time(NULL);
	if (t == -1) {
		warn("[datetime] Unable to obtain the current time");
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			die_errno("[datetime] snprintf");
		return;
	}
	if (localtime_r(&t, &now) == NULL) {
		warn("[datetime] Unable to determine local time");
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			die_errno("[datetime] snprintf");
		return;
	}
	(void)strftime(buf, MAX_COMP_LEN, "  %a %d %b %R", &now);
}

static void ram_free(char *buf)
{
	assert(buf != NULL);

	const char meminfo[] = "/proc/meminfo";
	FILE *fp;
	char total_str[MAX_COMP_LEN], free_str[MAX_COMP_LEN];
	uintmax_t free;

	fp = fopen(meminfo, "r");
	if (fp == NULL) {
		warn("[ram_free] Unable to open %s", meminfo);
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			die_errno("[ram_free] snprintf");
		return;
	}
	if (fscanf(fp,
		   "MemTotal: %s kB\n"
		   "MemFree: %s kB\n",
		   total_str, free_str) == EOF) {
		warn("[ram_free] Unable to parse %s", meminfo);
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			die_errno("[ram_free] snprintf");
		if (fclose(fp) == EOF)
			die_errno("[ram_free] fclose");
		return;
	}
	if (fclose(fp) == EOF)
		die_errno("[ram_free] fclose");

	free = strtoumax(free_str, NULL, 0);
	if (free == 0) {
		warn("[ram_free] Unable to parse %s", meminfo);
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			die_errno("[ram_free] snprintf");
		return;
	}

	fmt_human(free_str, LEN(free_str), free * K_IEC, K_IEC);
	if (snprintf(buf, MAX_COMP_LEN, "  %s", free_str) < 0)
		die_errno("[ram_free] snprintf");
}

static void terminate(const int UNUSED(signo))
{
	done = true;
}

static void flag_signal(const int signo)
{
	sigs_recv[signo - SIGRTMIN] = true;
}

static int install_signal_handlers(void)
{
	struct sigaction sa = { 0 };
	int signo;
	int nsigs = 0;

	sa.sa_flags = SA_RESTART;
	sa.sa_handler = flag_signal;

	for (size_t i = 0; i < NCOMPONENTS; i++) {
		signo = components[i].signo;
		if (signo >= 0) {
			if (sigaction(SIGRTMIN + signo, &sa, NULL) < 0)
				die_errno("sigaction");
			nsigs++;
		}
	}

	sigs_recv = calloc(nsigs, sizeof *sigs_recv);

	return nsigs;
}

static void *thread_print_status(void *arg)
{
	bool to_stdout;
	Display *dpy;
	int s, n;
	size_t i, len;
	char status[sizeof component_bufs];

	/* Unpack arg */
	to_stdout = ((struct targ_status *)arg)->to_stdout;
	dpy = ((struct targ_status *)arg)->dpy;
	free(arg);

	while (true) {
		s = pthread_mutex_lock(&bufs_mutex);
		if (s != 0)
			die_errnum(s, "pthread_mutex_lock");
		while (!is_updated) {
			s = pthread_cond_wait(&is_updated_cond, &bufs_mutex);
			if (s != 0)
				die_errnum(s, "pthread_cond_wait");
		}

		status[0] = '\0';
		for (i = len = 0; i < NCOMPONENTS - 1; i++, len += n) {
			n = snprintf(status + len, sizeof status, "%s%s",
				     component_bufs[i], divider);
			if (n < 0)
				die_errno("[thread_print_status] snprintf");
		}
		if (snprintf(status + len, sizeof status, "%s",
			     component_bufs[i]) < 0)
			die_errno("[thread_print_status] snprintf");

		is_updated = false;
		s = pthread_mutex_unlock(&bufs_mutex);
		if (s != 0)
			die_errnum(s, "pthread_mutex_unlock");

		if (to_stdout) {
			if (puts(status) == EOF)
				die_errno("[thread_print_status] puts");
			if (fflush(stdout) == EOF)
				die_errno("[thread_print_status] fflush");
		} else {
			if (XStoreName(dpy, DefaultRootWindow(dpy), status) < 0)
				die("XStoreName: Allocation failed");
			XFlush(dpy);
		}
	}

	return NULL;
}

static void *thread_upd_repeating(void *arg)
{
	void (*update)(char *);
	unsigned nsecs;
	unsigned posn;
	int s;
	char buf[MAX_COMP_LEN];

	/* Unpack arg */
	update = ((struct targ_updater *)arg)->component.update;
	nsecs = ((struct targ_updater *)arg)->component.sleep_secs;
	posn = ((struct targ_updater *)arg)->posn;
	free(arg);

	s = pthread_detach(pthread_self());
	if (s != 0)
		die_errnum(s, "pthread_detach");

	/* Component-update loop */
	while (true) {
		update(buf);
		s = pthread_mutex_lock(&bufs_mutex);
		if (s != 0)
			die_errnum(s, "pthread_mutex_lock");
		static_assert(sizeof(component_bufs[posn]) >= sizeof(buf),
			      "component_buf must be at least as large as buf");
		memcpy(component_bufs[posn], buf, sizeof buf);
		is_updated = true;
		s = pthread_mutex_unlock(&bufs_mutex);
		if (s != 0)
			die_errnum(s, "pthread_mutex_unlock");

		s = pthread_cond_signal(&is_updated_cond);
		if (s != 0)
			die_errnum(s, "pthread_cond_signal");

		sleep(nsecs);
	}

	return NULL;
}

static void *thread_upd_single(void *arg)
{
	void (*update)(char *);
	unsigned posn;
	int s;
	char buf[MAX_COMP_LEN];

	/* Unpack arg */
	update = ((struct targ_updater *)arg)->component.update;
	posn = ((struct targ_updater *)arg)->posn;
	free(arg);

	s = pthread_detach(pthread_self());
	if (s != 0)
		die_errnum(s, "pthread_detach");

	update(buf);
	s = pthread_mutex_lock(&bufs_mutex);
	if (s != 0)
		die_errnum(s, "pthread_mutex_lock");
	static_assert(sizeof(component_bufs[posn]) >= sizeof(buf),
		      "component_buf must be at least as large as buf");
	memcpy(component_bufs[posn], buf, sizeof buf);
	is_updated = true;
	s = pthread_mutex_unlock(&bufs_mutex);
	if (s != 0)
		die_errnum(s, "pthread_mutex_unlock");

	s = pthread_cond_signal(&is_updated_cond);
	if (s != 0)
		die_errnum(s, "pthread_cond_signal");

	return NULL;
}

/*
 * Create the thread for printing the status bar
 */
static void create_thread_print_status(bool to_stdout, Display *dpy)
{
	struct targ_status *arg;
	pthread_t tid;
	int ret;

	arg = malloc(sizeof *arg);
	arg->to_stdout = to_stdout;
	arg->dpy = dpy;
	ret = pthread_create(&tid, NULL, thread_print_status, arg);
	if (ret != 0)
		die_errnum(ret, "pthread_create");
}

/*
 * Create threads for the repeating updaters.
 */
static void create_threads_repeating(void)
{
	struct targ_updater *arg;
	pthread_t tid;
	int ret;

	for (size_t i = 0; i < NCOMPONENTS; i++) {
		/* Is it a repeating component? */
		if (components[i].sleep_secs >= 0) {
			arg = malloc(sizeof *arg);
			arg->component = components[i];
			arg->posn = i;

			/* Assume the thread routine frees arg */
			ret = pthread_create(&tid, NULL, thread_upd_repeating,
					     arg);
			if (ret != 0)
				die_errnum(ret, "pthread_create");
		}
	}
}

static void create_threads_single(const int signo)
{
	struct targ_updater *arg;
	pthread_t tid;
	int ret;

	/* Find components that specify this signal */
	for (size_t i = 0; i < NCOMPONENTS; i++) {
		if (components[i].signo == signo) {
			arg = malloc(sizeof *arg);
			arg->component = components[i];
			arg->posn = i;

			/* Assume the thread routine frees arg */
			ret = pthread_create(&tid, NULL, thread_upd_single,
					     arg);
			if (ret != 0)
				die_errnum(ret, "pthread_create");
		}
	}
}

static void process_signals(const int nsigs)
{
	for (int i = 0; i < nsigs; i++)
		if (sigs_recv[i]) {
			create_threads_single(i);
			sigs_recv[i] = false;
		}
}

int main(int argc, char *argv[])
{
	int opt;
	bool to_stdout = false;
	Display *dpy = NULL;
	struct sigaction sa;
	int nsigs;

	while ((opt = getopt(argc, argv, "s")) != -1) {
		switch (opt) {
		case 's':
			to_stdout = true;
			break;
		case '?':
			(void)fprintf(stderr, "Usage: %s [-s]\n", argv[0]);
			exit(EXIT_FAILURE);
			break;
		default:
			die("Unexpected case in switch()");
		}
	}

	if (!to_stdout) {
		dpy = XOpenDisplay(NULL);
		if (dpy == NULL)
			die("XOpenDisplay: Failed to open display");
	}

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = terminate;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sa.sa_flags |= SA_RESTART;

	nsigs = install_signal_handlers();

	create_thread_print_status(to_stdout, dpy);
	create_threads_repeating();

	/* Wait for signals to create single-update threads */
	while (!done) {
		process_signals(nsigs);
		pause();
	}

	if (!to_stdout) {
		XStoreName(dpy, DefaultRootWindow(dpy), NULL);
		if (XCloseDisplay(dpy) < 0)
			die("XCloseDisplay: Failed to close display");
	}

	return EXIT_SUCCESS;
}
