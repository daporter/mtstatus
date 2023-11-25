#include "../config.h"
#include "util.h"

#include <X11/Xlib.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N_COMPONENTS ((sizeof component_defns) / (sizeof(sbar_comp_defn_t)))

#define MAXLEN 128

typedef struct {
	unsigned id;
	char *buf;
	sbar_updater_t update;
	const char *args;
	time_t interval;
	int signum;
	pthread_t thr_repeating;
	pthread_t thr_async;
	struct sbar *sbar;
} sbar_comp_t;

typedef struct sbar {
	char *comp_bufs;
	uint8_t ncomponents;
	sbar_comp_t *components;
	bool dirty;
	pthread_mutex_t mutex;
	pthread_cond_t dirty_cond;
	pthread_t thread;
} sbar_t;

bool to_stdout = false;
Display *dpy = NULL;

void die(int code)
{
	fprintf(stderr, "mtstatus: fatal: %s\n", strerror(code));
	exit(EXIT_FAILURE);
}

void sbar_flush_on_dirty(sbar_t *sbar, char *buf, const size_t bufsize)
{
	int i = 0;
	char *ptr = buf;
	char *end = buf + bufsize;
	const char *cbuf;
	int r;

	/*
	 * Maintain the status bar "dirty" invariant.
	 */
	r = pthread_mutex_lock(&sbar->mutex);
	if (r != 0)
		die(r);
	while (!sbar->dirty) {
		r = pthread_cond_wait(&sbar->dirty_cond, &sbar->mutex);
		if (r != 0)
			die(r);
	}
	for (i = 0; (ptr < end) && (i < sbar->ncomponents - 1); i++) {
		cbuf = sbar->components[i].buf;
		if (strlen(cbuf) > 0) {
			ptr = util_cat(ptr, end, cbuf);
			ptr = util_cat(ptr, end, DIVIDER);
		}
	}
	if (ptr < end) {
		cbuf = sbar->components[i].buf;
		if (strlen(cbuf) > 0)
			ptr = util_cat(ptr, end, cbuf);
	}
	*ptr = '\0';

	sbar->dirty = false;
	r = pthread_mutex_unlock(&sbar->mutex);
	if (r != 0)
		die(r);
}

void sbar_component_update(const sbar_comp_t *c)
{
	char tmpbuf[MAXLEN];
	int r;
	comp_ret_t ret;

	ret = c->update(tmpbuf, sizeof(tmpbuf), c->args);
	if (!ret.ok)
		fprintf(stderr, "Error updating component: %s\n", ret.message);

	/*
	 * Maintain the status bar "dirty" invariant.
	 */
	r = pthread_mutex_lock(&c->sbar->mutex);
	if (r != 0)
		die(r);
	static_assert(MAXLEN >= sizeof(tmpbuf),
		      "size of component buffer < sizeof(tmpbuf)");
	memcpy(c->buf, tmpbuf, sizeof(tmpbuf));
	c->sbar->dirty = true;
	r = pthread_cond_signal(&c->sbar->dirty_cond);
	if (r != 0)
		die(r);
	r = pthread_mutex_unlock(&c->sbar->mutex);
	if (r != 0)
		die(r);
}

void *thread_flush(void *arg)
{
	sbar_t *sbar = (sbar_t *)arg;
	char status[N_COMPONENTS * MAXLEN];

	while (true) {
		sbar_flush_on_dirty(sbar, status, LEN(status));

		if (to_stdout) {
			if (puts(status) == EOF)
				die(errno);
			if (fflush(stdout) == EOF)
				die(errno);
		} else {
			XStoreName(dpy, DefaultRootWindow(dpy), status);
			XFlush(dpy);
		}
	}

	return NULL;
}

void *thread_repeating(void *arg)
{
	const sbar_comp_t *c = (sbar_comp_t *)arg;

	while (true) {
		sleep((unsigned)c->interval);
		sbar_component_update(c);
	}
	return NULL;
}

void *thread_async(void *arg)
{
	sbar_comp_t *c = (sbar_comp_t *)arg;
	sigset_t sigset;
	int sig, r;

	if (sigemptyset(&sigset) < 0)
		die(errno);
	if (sigaddset(&sigset, c->signum) < 0)
		die(errno);

	while (true) {
		r = sigwait(&sigset, &sig);
		if (r == -1)
			die(r);
		assert(sig == c->signum && "unexpected signal received");
		sbar_component_update(c);
	}

	return NULL;
}

void *thread_once(void *arg)
{
	const sbar_comp_t *c = (sbar_comp_t *)arg;
	sbar_component_update(c);
	return NULL;
}

void sbar_create(sbar_t *sbar, const uint8_t ncomponents,
		 const sbar_comp_defn_t *comp_defns)
{
	sbar_comp_t *cp;
	sigset_t sigset;
	int r;

	sbar->comp_bufs = calloc(ncomponents * (size_t)MAXLEN, sizeof(char));
	if (sbar->comp_bufs == NULL)
		die(errno);
	sbar->ncomponents = ncomponents;
	sbar->components = calloc(ncomponents, sizeof(sbar_comp_t));
	if (sbar->components == NULL)
		die(errno);
	sbar->dirty = false;
	r = pthread_mutex_init(&sbar->mutex, NULL);
	if (r != 0)
		die(r);
	r = pthread_cond_init(&sbar->dirty_cond, NULL);
	if (r != 0)
		die(r);

	/*
	 * The signal for which each asynchronous component thread will wait
	 * must be masked in all other threads.  This ensures that the signal
	 * will never be delivered to any other thread.  We set the mask here
	 * since all threads inherit their signal mask from their creator.
	 */
	if (sigemptyset(&sigset) < 0)
		die(errno);

	/* Create the components */
	for (unsigned i = 0; i < ncomponents; i++) {
		cp = &sbar->components[i];

		cp->id = i;
		cp->buf = sbar->comp_bufs + ((size_t)MAXLEN * i);
		static_assert(sizeof(NO_VAL_STR) <= MAXLEN,
			      "NO_VAL_STR too large");
		memcpy(cp->buf, NO_VAL_STR, sizeof(NO_VAL_STR));
		cp->update = comp_defns[i].update;
		cp->args = comp_defns[i].args;
		cp->interval = comp_defns[i].interval;
		cp->signum = comp_defns[i].signum;
		if (cp->signum >= 0) {
			/*
			 * Assume ‘signum’ specifies a real-time signal number
			 * and adjust the value accordingly.
			 */
			cp->signum += SIGRTMIN;
			if (sigaddset(&sigset, cp->signum) < 0)
				die(errno);
		}

		cp->sbar = sbar;
	}

	r = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if (r != 0)
		die(r);
}

void sbar_start(sbar_t *sbar)
{
	pthread_attr_t attr;
	pthread_t tid;
	sbar_comp_t *c;
	int r;

	r = pthread_attr_init(&attr);
	if (r != 0)
		die(r);
	r = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (r != 0)
		die(r);
	r = pthread_create(&sbar->thread, &attr, thread_flush, sbar);
	if (r != 0)
		die(r);

	for (uint8_t i = 0; i < sbar->ncomponents; i++) {
		c = &sbar->components[i];

		r = pthread_create(&tid, &attr, thread_once, c);
		if (r != 0)
			die(r);

		if (c->interval >= 0) {
			r = pthread_create(&c->thr_repeating, &attr,
					   thread_repeating, c);
			if (r != 0)
				die(r);
		}
		if (c->signum >= 0) {
			r = pthread_create(&c->thr_async, &attr, thread_async,
					   c);
			if (r != 0)
				die(r);
		}
	}
}

void usage(FILE *f)
{
	assert(f != NULL);
	fputs("Usage: mtstatus [-h] [-s]\n", f);
	fputs("  -h        Print this help message and exit\n", f);
	fputs("  -s        Output to stdout\n", f);
}

int main(int argc, char *argv[])
{
	char pidfile[MAXLEN];
	sbar_t sbar;

	int option;
	while ((option = getopt(argc, argv, "hs")) != -1) {
		switch (option) {
		case 'h':
			usage(stdout);
			exit(EXIT_SUCCESS);
		case 's':
			to_stdout = true;
			break;
		default:
			usage(stderr);
			exit(EXIT_FAILURE);
		}
	}

	if (!to_stdout) {
		/* Save the pid to a file so it’s available to shell commands */
		FILE *f;
		snprintf(pidfile, sizeof(pidfile), "/tmp/%s.pid",
			 basename(argv[0]));
		f = fopen(pidfile, "w");
		if (f == NULL)
			die(errno);
		if (fprintf(f, "%ld", (long)getpid()) < 0)
			die(errno);
		fclose(f);

		dpy = XOpenDisplay(NULL);
		if (dpy == NULL)
			die(errno);
	}

	/* SIGINT and SIGTERM must be delivered only to the initial thread */
	sigset_t sigset;
	if (sigemptyset(&sigset) < 0)
		die(errno);
	if (sigaddset(&sigset, SIGINT) < 0)
		die(errno);
	if (sigaddset(&sigset, SIGTERM) < 0)
		die(errno);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	/* Start the status bar */
	sbar_create(&sbar, N_COMPONENTS, component_defns);
	sbar_start(&sbar);

	/* Wait for SIGINT, SIGTERM */
	int sig, r;
	r = sigwait(&sigset, &sig);
	if (r == -1)
		die(r);

	switch (sig) {
	case SIGINT:
		puts("SIGINT received.\n");
		break;
	case SIGTERM:
		puts("SIGTERM received.\n");
		break;
	default:
		puts("Unexpected signal received.\n");
	}

	if (!to_stdout) {
		XStoreName(dpy, DefaultRootWindow(dpy), NULL);
		XCloseDisplay(dpy);

		if (remove(pidfile) < 0)
			fprintf(stderr, "Unable to remove %s", pidfile);
	}
}
