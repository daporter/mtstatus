#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * TODO:
 *   - check return values of all library functions and system calls
 *   - handle async component update (via signal)
 *   - add more component functions (see ‘syscalls(2) manpage’)
 *   - read about writing daemons in Kerrisk
 *   - add asserts (see K&R ‘assert.h’)
 */

static void ram_free(char *buf);
static void load_avg(char *buf);
static void datetime(char *buf);

/*
 * A status bar component.
 */
struct sb_component {
	void (*func)(char *);
	int sleep_secs;
};

/*
 * The components that make up the status bar.
 *
 * The order of the elements defines the order of components as they appear in
 * the status bar.
 */
static const struct sb_component components[] = {
	/* function, sleep */
	{ ram_free, 2 },
	{ load_avg, 2 },
	{ datetime, 1 },
};

/*
 * Text that indicates no value could be retrieved.
 */
static const char unknown_str[] = "n/a";

/*
 * Argument passed to the thread routine.
 */
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
		perror("time");
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			perror("snprintf");
		return;
	}
	if (localtime_r(&t, &now) == NULL) {
		perror("localtime_r");
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			perror("snprintf");
		return;
	}
	if (strftime(buf, MAX_COMP_LEN, "%T", &now) == 0)
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			perror("snprintf");
}

static void load_avg(char *buf)
{
	double avgs[1];

	if (getloadavg(avgs, 1) == -1) {
		perror("getloadavg");
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			perror("snprintf");
		return;
	}
	if (snprintf(buf, MAX_COMP_LEN, "%.2f", avgs[0]) < 0)
		perror("snprintf");
}

static void ram_free(char *buf)
{
	FILE *fp;
	char total_str[MAX_COMP_LEN], free_str[MAX_COMP_LEN];
	uintmax_t free;

	fp = fopen("/proc/meminfo", "r");
	if (fp == NULL) {
		perror("fopen");
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			perror("snprintf");
		return;
	}
	if (fscanf(fp,
		   "MemTotal: %s kB\n"
		   "MemFree: %s kB\n",
		   total_str, free_str) == EOF) {
		perror("fscanf");
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			perror("snprintf");
		if (fclose(fp) == EOF)
			perror("fclose");
		return;
	}
	if (fclose(fp) == EOF)
		perror("fclose");

	free = strtoumax(free_str, NULL, 0);
	if (free == 0) {
		(void)fprintf(stderr, "ram_free: unable to read MemFree\n");
		if (snprintf(buf, MAX_COMP_LEN, "%s", unknown_str) < 0)
			perror("snprintf");
		return;
	}

	if (snprintf(buf, MAX_COMP_LEN, "%ju", free) < 0)
		perror("snprintf");
}

static void *thread(void *arg)
{
	void (*func)(char *);
	int nsecs;
	unsigned posn;
	char output[MAX_COMP_LEN];

	/* Unpack arg. */
	func = ((struct targ *)arg)->sb_component.func;
	nsecs = ((struct targ *)arg)->sb_component.sleep_secs;
	posn = ((struct targ *)arg)->posn;
	free(arg);

	pthread_detach(pthread_self());

	while (true) {
		func(output);
		pthread_mutex_lock(&bufs_mutex);
		memcpy(bufs[posn], output, MAX_COMP_LEN);
		is_updated = true;
		pthread_mutex_unlock(&bufs_mutex);
		pthread_cond_signal(&is_updated_cond);

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

	for (unsigned i = 0; i < NCOMPONENTS; i++) {
		arg = malloc(sizeof *arg);
		arg->sb_component = components[i];
		arg->posn = i;
		/* assume the thread frees arg */
		pthread_create(&tid, NULL, thread, arg);
	}

	while (true) {
		pthread_mutex_lock(&bufs_mutex);
		if (!is_updated)
			pthread_cond_wait(&is_updated_cond, &bufs_mutex);
		print_bufs();
		is_updated = false;
		pthread_mutex_unlock(&bufs_mutex);
	}

	return EXIT_SUCCESS;
}
