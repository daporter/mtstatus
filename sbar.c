/* TODO:
   - Add handlers for SIGINT, etc., for clean shutdown.
   - Refactor error handling.
   - Handle async component update (via signal).
   - Add more component functions (see ‘syscalls(2) manpage’). */

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
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
	unsigned sleep_secs;
};

/* The components that make up the status bar.

   Each element consists of an updater function and a sleep interval (in
   seconds).  The order of the elements defines the order of components in the
   status bar. */
static const struct component components[] = {
	/* function, sleep */
	{ ram_free, 2 },
	{ datetime, 2 },
	{ datetime, 1 },
};

/* Argument passed to the thread routine */
struct targ {
	struct component component;
	unsigned posn;
};

#define NCOMPONENTS  ((sizeof components) / (sizeof(struct component)))
#define MAX_COMP_LEN 128

/* Each thread writes to its own output buffer */
static char component_bufs[NCOMPONENTS][MAX_COMP_LEN];
static pthread_mutex_t bufs_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool is_updated = false;
static pthread_cond_t is_updated_cond = PTHREAD_COND_INITIALIZER;

/* Text that indicates no value could be obtained */
static const char unknown_str[] = "n/a";
static_assert(sizeof(unknown_str) <= sizeof(component_bufs[0]),
	      "unknown_str must be no bigger than component_buf");

static void datetime(char *buf)
{
	time_t t;
	struct tm now;

	t = time(NULL);
	if (t == -1) {
		err_msg("[datetime] Unable to obtain the current time");
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			err_exit("[datetime] snprintf");
		return;
	}
	if (localtime_r(&t, &now) == NULL) {
		err_msg("[datetime] Unable to determine local time");
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
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
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			err_exit("[ram_free] snprintf");
		return;
	}
	if (fscanf(fp,
		   "MemTotal: %s kB\n"
		   "MemFree: %s kB\n",
		   total_str, free_str) == EOF) {
		err_msg("[ram_free] Unable to parse %s", meminfo);
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
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
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			err_exit("[ram_free] snprintf");
		return;
	}

	if (snprintf(buf, MAX_COMP_LEN, "%ju", free) < 0)
		err_exit("[ram_free] snprintf");
}

static void *thread(void *arg)
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

static void print_component_bufs(void)
{
	unsigned i;

	for (i = 0; i < NCOMPONENTS - 1; i++)
		printf("%s  |  ", component_bufs[i]);
	printf("%s\n", component_bufs[i]);
}

int main(void)
{
	struct targ *arg;
	pthread_t tid;
	int s;

	/* Create the threads */
	for (unsigned i = 0; i < NCOMPONENTS; i++) {
		arg = malloc(sizeof *arg);
		arg->component = components[i];
		arg->posn = i;

		/* We assume the thread routine frees arg */
		s = pthread_create(&tid, NULL, thread, arg);
		if (s != 0)
			err_exit_en(s, "pthread_create");
	}

	/* Print-status-bar loop */
	while (true) {
		s = pthread_mutex_lock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_lock");
		while (!is_updated) {
			s = pthread_cond_wait(&is_updated_cond, &bufs_mutex);
			if (s != 0)
				err_exit_en(s, "pthread_cond_wait");
		}
		print_component_bufs();
		is_updated = false;
		s = pthread_mutex_unlock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_unlock");
	}

	return EXIT_SUCCESS;
}
