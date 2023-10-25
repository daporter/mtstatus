/* TODO:
  - Refactor error handling.
  - Handle async component update (via signal).
  - Add more component functions (see ‘syscalls(2) manpage’).
  - Add asserts (see K&R ‘assert.h’; Seacord ch. 11). */

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
struct sb_component {
	void (*func)(char *);
	int sleep_secs;
};

/* The components that make up the status bar.
 
   Each element consists of an updater function and a sleep interval (in
   seconds).  The order of the elements defines the order of components in the
   status bar. */
static const struct sb_component components[] = {
	/* function, sleep */
	{ ram_free, 2 },
	{ datetime, 2 },
	{ datetime, 1 },
};

/* Text that indicates no value could be obtained */
static const char unknown_str[] = "n/a";

/* Argument passed to the thread routine */
struct targ {
	struct sb_component sb_component;
	unsigned posn;
};

#define NCOMPONENTS  ((sizeof components) / (sizeof(struct sb_component)))
#define MAX_COMP_LEN 128

/* Each thread writes to its own output buffer */
static char bufs[NCOMPONENTS][MAX_COMP_LEN];
static pthread_mutex_t bufs_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool is_updated = false;
static pthread_cond_t is_updated_cond = PTHREAD_COND_INITIALIZER;

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
	const char *meminfo = "/proc/meminfo";
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
	void (*func)(char *);
	int nsecs, s;
	unsigned posn;
	char output[MAX_COMP_LEN];

	/* Unpack arg */
	func = ((struct targ *)arg)->sb_component.func;
	nsecs = ((struct targ *)arg)->sb_component.sleep_secs;
	posn = ((struct targ *)arg)->posn;
	free(arg);

	s = pthread_detach(pthread_self());
	if (s != 0)
		err_exit_en(s, "pthread_detach");

	while (true) {
		func(output);
		s = pthread_mutex_lock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_lock");
		memcpy(bufs[posn], output, MAX_COMP_LEN);
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

static void print_bufs(void)
{
	unsigned i;

	for (i = 0; i < NCOMPONENTS - 1; i++)
		printf("%s  |  ", bufs[i]);
	printf("%s\n", bufs[i]);
}

int main(void)
{
	struct targ *arg;
	pthread_t tid;
	int s;

	for (unsigned i = 0; i < NCOMPONENTS; i++) {
		arg = malloc(sizeof *arg);
		arg->sb_component = components[i];
		arg->posn = i;

		/* Assume the thread routine frees arg */
		s = pthread_create(&tid, NULL, thread, arg);
		if (s != 0)
			err_exit_en(s, "pthread_create");
	}

	while (true) {
		s = pthread_mutex_lock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_lock");
		if (!is_updated) {
			s = pthread_cond_wait(&is_updated_cond, &bufs_mutex);
			if (s != 0)
				err_exit_en(s, "pthread_cond_wait");
		}
		print_bufs();
		is_updated = false;
		s = pthread_mutex_unlock(&bufs_mutex);
		if (s != 0)
			err_exit_en(s, "pthread_mutex_unlock");
	}

	return EXIT_SUCCESS;
}
