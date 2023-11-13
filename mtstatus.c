#include <X11/Xlib.h>
#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "component.h"
#include "errors.h"
#include "sbar.h"
#include "util.h"
#include "config.h"

#define N_COMPONENTS ((sizeof component_defns) / (sizeof(sbar_comp_defn_t)))

/* Argument passed to the print-status thread */
struct targ_status {
	bool to_stdout;
	Display *dpy;
};

static volatile sig_atomic_t done;

/* Array of flags indicating which signals have been received and not yet
   processed.  Set by the signal handler. */
static volatile sig_atomic_t *signals_received;

static void terminate(UNUSED(int signum))
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
		signum = component_defns[i].signum;
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
	char status[N_COMPONENTS * SBAR_MAX_COMP_SIZE];

	/* Unpack arg */
	to_stdout = ((struct targ_status *)arg)->to_stdout;
	dpy = ((struct targ_status *)arg)->dpy;
	Free(arg);

	Pthread_detach(Pthread_self());

	while (true) {
		sbar_render_on_dirty(status, LEN(status));

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
	time_t interval = component_defns[posn].interval;

	// TODO(david): Create this thread already detached.
	Pthread_detach(Pthread_self());

	while (true) {
		Sleep(interval);
		sbar_update_component(posn);
	}

	return NULL;
}

static void *thread_upd_once(void *arg)
{
	size_t posn = (size_t)arg;

	Pthread_detach(Pthread_self());

	sbar_update_component(posn);

	return NULL;
}

/* Create the thread for printing the status bar */
static void create_thread_print_status(Display *dpy, bool to_stdout)
{
	struct targ_status *arg;
	pthread_t tid;

	arg = Calloc(1, sizeof *arg);
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
		if (component_defns[i].interval >= 0)
			Pthread_create(&tid, NULL, thread_upd_repeating,
				       (void *)i);
}

/* Create threads for running updaters once to get an initial value */
static void create_threads_initial(void)
{
	pthread_t tid;

	for (size_t i = 0; i < N_COMPONENTS; i++)
		Pthread_create(&tid, NULL, thread_upd_once, (void *)i);
}

static void create_threads_async(const int signum)
{
	pthread_t tid;

	/* Find components that specify this signal */
	for (size_t i = 0; i < N_COMPONENTS; i++)
		if (component_defns[i].signum == signum)
			Pthread_create(&tid, NULL, thread_upd_once, (void *)i);
}

static void process_signals(const int nsigs)
{
	for (int i = 0; i < nsigs; i++)
		if (signals_received[i]) {
			create_threads_async(i);
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

	sbar_init(component_defns, N_COMPONENTS);

	create_thread_print_status(dpy, to_stdout);
	create_threads_initial();
	create_threads_repeating();

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
