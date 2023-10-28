/* TODO:
   - Add handlers for SIGINT, etc., for clean shutdown.
   - Output to X.
   - Refactor error handling.
   - Add more component functions (see ‘syscalls(2) manpage’). */

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
	{ datetime, 2, -1 },
	{ datetime, 1, 0 },
	{ datetime, -1, 1 },
};

/* Argument passed to a single-update thread */
struct targ {
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

/*
 * Array of flags indicating which signals have been received and not yet
   processed.  Set by the signal handler.
 */
static volatile sig_atomic_t *sigs_received;

static void datetime(char *buf)
{
	time_t t;
	struct tm now;

	t = time(NULL);
	if (t == -1) {
		err_msg("[datetime] Unable to obtain the current time");
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			err_exit("[datetime] snprintf");
		return;
	}
	if (localtime_r(&t, &now) == NULL) {
		err_msg("[datetime] Unable to determine local time");
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			err_exit("[datetime] snprintf");
		return;
	}
	(void)strftime(buf, MAX_COMP_LEN, "%T", &now);
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
		err_msg("[ram_free] Unable to open %s", meminfo);
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			err_exit("[ram_free] snprintf");
		return;
	}
	if (fscanf(fp,
		   "MemTotal: %s kB\n"
		   "MemFree: %s kB\n",
		   total_str, free_str) == EOF) {
		err_msg("[ram_free] Unable to parse %s", meminfo);
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			err_exit("[ram_free] snprintf");
		if (fclose(fp) == EOF)
			err_exit("[ram_free] fclose");
		return;
	}
	if (fclose(fp) == EOF)
		err_exit("[ram_free] fclose");

	free = strtoumax(free_str, NULL, 0);
	if (free == 0) {
		err_msg("[ram_free] Unable to parse %s", meminfo);
		if (snprintf(buf, MAX_COMP_LEN, "%s", no_val_str) < 0)
			err_exit("[ram_free] snprintf");
		return;
	}

	if (snprintf(buf, MAX_COMP_LEN, "%ju", free) < 0)
		err_exit("[ram_free] snprintf");
}

static void terminate(const int UNUSED(signo))
{
	sigs_received[signo - SIGRTMIN] = true;
	printf("Signal received: SIGRTMIN+%d\n", signo - SIGRTMIN);
}

int install_signal_handlers(void)
{
	struct sigaction sa = { 0 };
	int signo;
	int nsigs = 0;

	sa.sa_flags = SA_RESTART;
	sa.sa_handler = sig_handler;

	for (size_t i = 0; i < NCOMPONENTS; i++) {
		signo = components[i].signo;
		if (signo >= 0) {
			assert(SIGRTMIN + signo <= SIGRTMAX &&
			       "signal number must be <= SIGRTMAX");

			if (sigaction(SIGRTMIN + signo, &sa, NULL) < 0)
				err_exit("sigaction");
			printf("Created signal handler for SIGRTMAX+%d\n",
			       signo);
			nsigs++;
		}
	}

	sigs_received = calloc(nsigs, sizeof *sigs_received);

	return nsigs;
}

static void *thread_print_sbar(void *UNUSED(arg))
{
	int s;
	size_t i;

	while (true) {
		s = pthread_mutex_lock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_lock");
		while (!is_updated) {
			s = pthread_cond_wait(&is_updated_cond, &bufs_mutex);
			if (s != 0)
				err_exit_en(s, "pthread_cond_wait");
		}

		for (i = 0; i < NCOMPONENTS - 1; i++)
			printf("%s  |  ", component_bufs[i]);
		printf("%s\n", component_bufs[i]);

		is_updated = false;
		s = pthread_mutex_unlock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_unlock");
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
	update = ((struct targ *)arg)->component.update;
	nsecs = ((struct targ *)arg)->component.sleep_secs;
	posn = ((struct targ *)arg)->posn;
	free(arg);

	printf("Created repeating thread for components[%u] with sleep %u\n",
	       posn, nsecs);

	s = pthread_detach(pthread_self());
	if (s != 0)
		err_exit_en(s, "pthread_detach");

	/* Component-update loop */
	while (true) {
		update(buf);
		s = pthread_mutex_lock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_lock");
		static_assert(sizeof(component_bufs[posn]) >= sizeof(buf),
			      "component_buf must be at least as large as buf");
		memcpy(component_bufs[posn], buf, sizeof buf);
		is_updated = true;
		s = pthread_mutex_unlock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_unlock");

		s = pthread_cond_signal(&is_updated_cond);
		if (s != 0)
			err_exit_en(s, "pthread_cond_signal");

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
	update = ((struct targ *)arg)->component.update;
	posn = ((struct targ *)arg)->posn;
	free(arg);

	printf("Created single-update thread for components[%u]\n", posn);

	s = pthread_detach(pthread_self());
	if (s != 0)
		err_exit_en(s, "pthread_detach");

	update(buf);
	s = pthread_mutex_lock(&bufs_mutex);
	if (s != 0)
		err_exit_en(s, "pthread_mutex_lock");
	static_assert(sizeof(component_bufs[posn]) >= sizeof(buf),
		      "component_buf must be at least as large as buf");
	memcpy(component_bufs[posn], buf, sizeof buf);
	is_updated = true;
	s = pthread_mutex_unlock(&bufs_mutex);
	if (s != 0)
		err_exit_en(s, "pthread_mutex_unlock");

	s = pthread_cond_signal(&is_updated_cond);
	if (s != 0)
		err_exit_en(s, "pthread_cond_signal");

	return NULL;
}

/*
 * Create the thread for printing the status bar
 */
void create_thread_sbar(void)
{
	pthread_t tid;
	int ret;

	ret = pthread_create(&tid, NULL, thread_print_sbar, NULL);
	if (ret != 0)
		err_exit_en(ret, "pthread_create");
}

/*
 * Create threads for the repeating updaters.
 */
void create_threads_repeating(void)
{
	struct targ *arg;
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
				err_exit_en(ret, "pthread_create");
		}
	}
}

void create_threads_single(int signo)
{
	struct targ *arg;
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
				err_exit_en(ret, "pthread_create");
		}
	}
}

void process_signals(int nsigs)
{
	for (int i = 0; i < nsigs; i++)
		if (sigs_received[i]) {
			create_threads_single(i);
			sigs_received[i] = false;
		}
}

int main(void)
{
	int nsigs;
	
	nsigs = install_signal_handlers();
	create_thread_sbar();
	create_threads_repeating();

	/* Wait for signals to create single-update threads */
	while (true) {
		pause();
		process_signals(nsigs);
	}

	return EXIT_SUCCESS;
}
