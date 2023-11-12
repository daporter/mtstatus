#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "errors.h"
#include "sbar.h"
#include "util.h"

#define N_COMPONENTS ((sizeof components) / (sizeof(sbar_cmp_t)))

static_assert(LEN(divider) <= MAX_COMP_SIZE,
	      "divider must be no bigger than component length");

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

	Pthread_detach(Pthread_self());

	while (true) {
		sbar_cmp_update(posn);
		Sleep(sbar_cmp_get_sleep(posn));
	}

	return NULL;
}

static void *thread_upd_once(void *arg)
{
	size_t posn = (size_t)arg;

	Pthread_detach(Pthread_self());

	sbar_cmp_update(posn);

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
		if (components[i].sleep_secs >= 0)
			Pthread_create(&tid, NULL, thread_upd_repeating,
				       (void *)i);
}

/* Create threads for running signal-only updaters once to get an initial
   value */
static void create_threads_async_initial(void)
{
	pthread_t tid;

	for (size_t i = 0; i < N_COMPONENTS; i++)
		if (sbar_cmp_is_signal_only(i))
			Pthread_create(&tid, NULL, thread_upd_once, (void *)i);
}

static void create_threads_async(const int signum)
{
	pthread_t tid;

	/* Find components that specify this signal */
	for (size_t i = 0; i < N_COMPONENTS; i++)
		if (components[i].signum == signum)
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

	sbar_init(components, N_COMPONENTS, MAX_COMP_SIZE, divider, no_val_str);

	create_threads_repeating();
	create_threads_async_initial();
	create_thread_print_status(dpy, to_stdout);

	/* Wait for signals to create single-update threads */
	nsigs = install_signal_handlers();
	while (!done) {
		process_signals(nsigs);
		Pause();
	}

	/* TODO: Cancel theads */
	sbar_destroy();

	if (!to_stdout) {
		xStoreName(dpy, DefaultRootWindow(dpy), NULL);
		xCloseDisplay(dpy);
	}

	return EXIT_SUCCESS;
}
