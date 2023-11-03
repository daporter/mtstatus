/* TODO:
   - Add error and warning wrappers for library and system calls.
   - Add more component functions (see ‘syscalls(2) manpage’).
   - Add debugging output.
   - Refactor duplicated code. */

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "components.h"
#include "errors.h"
#include "status_bar.h"
#include "util.h"

#define MAX_COMP_SIZE 128
#define N_COMPONENTS  ((sizeof components) / (sizeof(component_t)))

const char divider[] = "  ";
static_assert(LEN(divider) <= MAX_COMP_SIZE,
	      "divider must be no bigger than component length");

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
component_t components[] = {
	/* function, arguments, sleep, signal */
	{ keyboard_indicators, NULL, -1,  0 },
	{ notmuch,	       NULL, -1,  1 },
	/* network traffic */
	{ load_avg,	       NULL,  2, -1 },
	{ ram_free,	       NULL,  2, -1 },
	{ disk_free,	       "/",  15, -1 },
	/* volume */
	/* wifi */
	{ datetime, "%a %d %b %R",   30, -1 },
};
/* clang-format on */

status_bar_t *status_bar;

/* Argument passed to the print-status thread */
struct targ_status {
	bool to_stdout;
	Display *dpy;
};

static volatile sig_atomic_t done;

/* Array of flags indicating which signals have been received and not yet
   processed.  Set by the signal handler. */
static volatile sig_atomic_t *signals_received;

static void terminate(const int unused(signum))
{
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

	for (size_t i = 0; i < N_COMPONENTS; i++) {
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
	char status[N_COMPONENTS * MAX_COMP_SIZE];

	/* Unpack arg */
	to_stdout = ((struct targ_status *)arg)->to_stdout;
	dpy = ((struct targ_status *)arg)->dpy;
	Free(arg);

	Pthread_detach(Pthread_self());

	while (true) {
		status_bar_print_on_dirty(status_bar, status, LEN(status));

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

	Pthread_detach(Pthread_self());

	while (true) {
		status_bar_component_update(status_bar, posn);
		Sleep(status_bar_component_get_sleep(status_bar, posn));
	}

	return NULL;
}

static void *thread_upd_single(void *arg)
{
	size_t posn = (size_t)arg;

	Pthread_detach(Pthread_self());

	status_bar_component_update(status_bar, posn);

	return NULL;
}

/* Create the thread for printing the status bar */
static void create_thread_print_status(Display *dpy, bool to_stdout)
{
	struct targ_status *arg;
	pthread_t tid;

	arg = Malloc(sizeof *arg);
	arg->to_stdout = to_stdout;
	arg->dpy = dpy;
	Pthread_create(&tid, NULL, thread_print_status, arg);
}

/* Create threads for the repeating updaters */
static void create_threads_repeating(void)
{
	pthread_t tid;

	for (size_t i = 0; i < N_COMPONENTS; i++)
		/* Is it a repeating component? */
		if (components[i].sleep_secs >= 0)
			Pthread_create(&tid, NULL, thread_upd_repeating,
				       (void *)i);
}

/* Create threads for running signal-only updaters once to get an initial
   value */
static void create_threads_sig_only_initial(void)
{
	pthread_t tid;

	for (size_t i = 0; i < N_COMPONENTS; i++)
		if (status_bar_component_signal_only(status_bar, i))
			Pthread_create(&tid, NULL, thread_upd_single,
				       (void *)i);
}

static void create_threads_single(const int signum)
{
	pthread_t tid;

	/* Find components that specify this signal */
	for (size_t i = 0; i < N_COMPONENTS; i++)
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

	status_bar = status_bar_create(components, N_COMPONENTS, MAX_COMP_SIZE,
				       divider);

	create_threads_repeating();
	create_threads_sig_only_initial();
	create_thread_print_status(dpy, to_stdout);

	/* Wait for signals to create single-update threads */
	nsigs = install_signal_handlers();
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
