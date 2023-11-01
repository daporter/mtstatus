/* TODO:
 * - Add more component functions (see ‘syscalls(2) manpage’).
 * - Add debugging output.
 * - Refactor duplicated code.
 */

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "components.h"
#include "config.h"
#include "errors.h"
#include "util.h"

/* Argument passed to the print-status thread */
struct targ_status {
	bool to_stdout;
	Display *dpy;
};

/* Each thread writes to its own output buffer */
static char component_bufs[NCOMPONENTS][MAX_COMP_LEN];
static pthread_mutex_t bufs_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool is_updated = false;
static pthread_cond_t is_updated_cond = PTHREAD_COND_INITIALIZER;

static_assert(sizeof(no_val_str) <= sizeof(component_bufs[0]),
	      "no_val_str must be no bigger than component_buf");

static volatile sig_atomic_t done;

/*
 * Array of flags indicating which signals have been received and not yet
   processed.  Set by the signal handler.
 */
static volatile sig_atomic_t *signals_received;

static void terminate(const int signum)
{
	UNUSED(signum);

	done = true;
}

static void flag_signal(const int signum)
{
	signals_received[signum - SIGRTMIN] = true;
}

static int install_signal_handlers(void)
{
	int signum;
	int nsigs = 0;

	for (size_t i = 0; i < NCOMPONENTS; i++) {
		signum = components[i].signum;
		if (signum >= 0) {
			Signal(SIGRTMIN + signum, flag_signal);
			nsigs++;
		}
	}

	signals_received = Calloc(nsigs, sizeof *signals_received);

	return nsigs;
}

static void *thread_print_status(void *arg)
{
	bool to_stdout;
	Display *dpy;
	int n;
	size_t i, len;
	char status[NCOMPONENTS * MAX_COMP_LEN];

	/* Unpack arg */
	to_stdout = ((struct targ_status *)arg)->to_stdout;
	dpy = ((struct targ_status *)arg)->dpy;
	Free(arg);

	while (true) {
		Pthread_mutex_lock(&bufs_mutex);
		while (!is_updated)
			Pthread_cond_wait(&is_updated_cond, &bufs_mutex);

		status[0] = '\0';
		for (i = len = 0; i < NCOMPONENTS - 1; i++, len += n)
			n = Snprintf(status + len, sizeof status, "%s%s",
				     component_bufs[i], divider);
		Snprintf(status + len, sizeof status, "%s", component_bufs[i]);

		is_updated = false;
		Pthread_mutex_unlock(&bufs_mutex);

		if (to_stdout) {
			Puts(status);
			Fflush(stdout);
		} else {
			xStoreName(dpy, DefaultRootWindow(dpy), status);
			xFlush(dpy);
		}
	}

	return NULL;
}

static void *thread_upd_repeating(void *arg)
{
	size_t posn = (size_t)arg;
	struct component c = components[posn];
	char buf[MAX_COMP_LEN];

	Pthread_detach(Pthread_self());

	/* Component-update loop */
	while (true) {
		c.update(buf, LEN(buf), c.args);
		Pthread_mutex_lock(&bufs_mutex);
		static_assert(LEN(component_bufs[posn]) >= LEN(buf),
			      "component_buf must be at least as large as buf");
		memcpy(component_bufs[posn], buf, LEN(buf));
		is_updated = true;
		Pthread_mutex_unlock(&bufs_mutex);
		Pthread_cond_signal(&is_updated_cond);
		Sleep(c.sleep_secs);
	}

	return NULL;
}

static void *thread_upd_single(void *arg)
{
	size_t posn = (size_t)arg;
	struct component c = components[posn];
	char buf[MAX_COMP_LEN];

	Pthread_detach(Pthread_self());

	c.update(buf, LEN(buf), c.args);
	Pthread_mutex_lock(&bufs_mutex);
	static_assert(LEN(component_bufs[posn]) >= LEN(buf),
		      "component_buf must be at least as large as buf");
	Memcpy(component_bufs[posn], buf, LEN(buf));
	is_updated = true;
	Pthread_mutex_unlock(&bufs_mutex);
	Pthread_cond_signal(&is_updated_cond);

	return NULL;
}

/*
 * Create the thread for printing the status bar
 */
static void create_thread_print_status(bool to_stdout, Display *dpy)
{
	struct targ_status *arg;
	pthread_t tid;

	arg = Malloc(sizeof *arg);
	arg->to_stdout = to_stdout;
	arg->dpy = dpy;
	Pthread_create(&tid, NULL, thread_print_status, arg);
}

/*
 * Create threads for the repeating updaters.
 */
static void create_threads_repeating(void)
{
	pthread_t tid;

	for (size_t i = 0; i < NCOMPONENTS; i++)
		/* Is it a repeating component? */
		if (components[i].sleep_secs >= 0)
			Pthread_create(&tid, NULL, thread_upd_repeating,
				       (void *)i);
}

static void create_threads_single(const int signum)
{
	pthread_t tid;

	/* Find components that specify this signal */
	for (size_t i = 0; i < NCOMPONENTS; i++)
		if (components[i].signum == signum)
			Pthread_create(&tid, NULL, thread_upd_single,
				       (void *)i);
}

static void process_signals(const int nsigs)
{
	for (int i = 0; i < nsigs; i++)
		if (signals_received[i]) {
			create_threads_single(i);
			signals_received[i] = false;
		}
}

int main(int argc, char *argv[])
{
	int opt;
	bool to_stdout = false;
	Display *dpy = NULL;
	int nsigs;

	while ((opt = getopt(argc, argv, "s")) != -1) {
		switch (opt) {
		case 's':
			to_stdout = true;
			break;
		case '?':
			app_error("Usage: %s [-s]", argv[0]);
			break;
		default:
			app_error("Unexpected case in switch()");
		}
	}

	if (!to_stdout)
		dpy = xOpenDisplay(NULL);

	Signal(SIGINT, terminate);
	Signal(SIGTERM, terminate);

	nsigs = install_signal_handlers();

	create_thread_print_status(to_stdout, dpy);
	create_threads_repeating();

	/* Wait for signals to create single-update threads */
	while (!done) {
		process_signals(nsigs);
		Pause();
	}

	if (!to_stdout) {
		xStoreName(dpy, DefaultRootWindow(dpy), NULL);
		xCloseDisplay(dpy);
	}

	return EXIT_SUCCESS;
}
