#include <X11/Xlib.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "component.h"
#include "config.h"
#include "errors.h"
#include "util.h"

#define N_COMPONENTS ((sizeof component_defns) / (sizeof(sbar_comp_defn_t)))

#define SBAR_MAX_COMP_SIZE (size_t)128

typedef struct component {
	char *buf;
	sbar_updater_t update;
	const char *args;
	time_t interval;
	int signum;
	pthread_t thr_repeating;
	pthread_t thr_async;
	struct sbar *sbar;
} component_t;

typedef struct sbar {
	char *component_bufs;
	uint8_t ncomponents;
	component_t *components;
	bool dirty;
	pthread_mutex_t mutex;
	pthread_cond_t dirty_cond;
	pthread_t thread;
} sbar_t;

static const char divider[] = "  ";
static const char no_val_str[] = "n/a";

static sigset_t signal_set;

static volatile sig_atomic_t done;

static bool to_stdout = false;
static Display *dpy = NULL;

static void terminate(UNUSED(int signum))
{
	done = true;
}

static void sbar_flush_on_dirty(sbar_t *sbar, char *buf, const size_t bufsize)
{
	uint8_t i = 0;
	char *ptr = buf;
	char *end = buf + bufsize;
	const char *cbuf;

	/*
	 * Maintain the status bar "dirty" invariant.
	 */
	Pthread_mutex_lock(&sbar->mutex);
	while (!sbar->dirty)
		Pthread_cond_wait(&sbar->dirty_cond, &sbar->mutex);

	for (i = 0; (ptr < end) && (i < sbar->ncomponents - 1); i++) {
		cbuf = sbar->components[i].buf;
		if (strlen(cbuf) > 0) {
			ptr = util_cat(ptr, end, cbuf);
			ptr = util_cat(ptr, end, divider);
		}
	}
	if (ptr < end) {
		cbuf = sbar->components[i].buf;
		if (strlen(cbuf) > 0)
			ptr = util_cat(ptr, end, cbuf);
	}
	*ptr = '\0';

	sbar->dirty = false;
	Pthread_mutex_unlock(&sbar->mutex);
}

static void *thread_status(void *arg)
{
	sbar_t *sbar = (sbar_t *)arg;
	char status[N_COMPONENTS * SBAR_MAX_COMP_SIZE];

	while (true) {
		sbar_flush_on_dirty(sbar, status, LEN(status));

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

static void sbar_component_update(const component_t *c)
{
	char tmpbuf[SBAR_MAX_COMP_SIZE];

	c->update(tmpbuf, SBAR_MAX_COMP_SIZE, c->args, no_val_str);

	/*
	 * Maintain the status bar "dirty" invariant.
	 */
	Pthread_mutex_lock(&c->sbar->mutex);
	Memcpy(c->buf, tmpbuf, SBAR_MAX_COMP_SIZE);
	c->sbar->dirty = true;
	Pthread_cond_signal(&c->sbar->dirty_cond);
	Pthread_mutex_unlock(&c->sbar->mutex);
}

static void *thread_repeating(void *arg)
{
	const component_t *c = (component_t *)arg;

	while (true) {
		Sleep(c->interval);
		sbar_component_update(c);
	}
	return NULL;
}

static void *thread_once(void *arg)
{
	const component_t *c = (component_t *)arg;

	sbar_component_update(c);
	return NULL;
}

static void *thread_async(void *arg)
{
	component_t *c = (component_t *)arg;
	sigset_t myset;
	int recv;

	/* Wait only for this component’s signal number */
	Sigemptyset(&myset);
	Sigaddset(&signal_set, c->signum);

	while (true) {
		Sigwait(&signal_set, &recv);
		assert(recv == c->signum && "unexpected signal received");
		sbar_component_update(c);
	}

	return NULL;
}

static void sbar_create(sbar_t *sbar, const uint8_t ncomponents,
			const sbar_comp_defn_t *comp_defns)
{
	component_t *c;

	sbar->component_bufs =
		Calloc(ncomponents * SBAR_MAX_COMP_SIZE, sizeof(char));
	sbar->ncomponents = ncomponents;
	sbar->components = Calloc(ncomponents, sizeof(component_t));
	sbar->dirty = false;
	Pthread_mutex_init(&sbar->mutex, NULL);
	Pthread_cond_init(&sbar->dirty_cond, NULL);

	Sigemptyset(&signal_set);

	/* Create the components */
	for (uint8_t i = 0; i < ncomponents; i++) {
		c = &sbar->components[i];

		c->buf = sbar->component_bufs + (SBAR_MAX_COMP_SIZE * i);
		Strcpy(c->buf, no_val_str);
		c->update = comp_defns[i].update;
		c->args = comp_defns[i].args;
		c->interval = comp_defns[i].interval;

		/*
		 * The signal for which each asynchronous component thread will
		 * wait must be masked in all other threads.  This ensures that
		 * the signal will never be delivered to any other thread.  We
		 * set the mask here since all threads inherit their signal mask
		 * from their creator.
		 */
		c->signum = comp_defns[i].signum;
		if (c->signum >= 0) {
			/*
			 * Assume ‘signum’ specifies a real-time signal number
			 * and adjust the value accordingly.
			 */
			c->signum += SIGRTMIN;
			Sigaddset(&signal_set, c->signum);
		}

		c->sbar = sbar;
	}

	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
}

static void sbar_start(sbar_t *sbar)
{
	pthread_attr_t attr;
	pthread_t tid;
	component_t *c;

	Pthread_attr_init(&attr);
	Pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	Pthread_create(&sbar->thread, &attr, thread_status, sbar);

	for (uint8_t i = 0; i < sbar->ncomponents; i++) {
		c = &sbar->components[i];

		Pthread_create(&tid, &attr, thread_once, c);

		if (c->interval >= 0)
			Pthread_create(&c->thr_repeating, &attr,
				       thread_repeating, c);
		if (c->signum >= 0)
			Pthread_create(&c->thr_async, &attr, thread_async, c);
	}
}

int main(int argc, char *argv[])
{
	int opt;
	sbar_t sbar;

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

	sbar_create(&sbar, N_COMPONENTS, component_defns);
	sbar_start(&sbar);

	while (!done)
		Pause();

	if (!to_stdout) {
		xStoreName(dpy, DefaultRootWindow(dpy), NULL);
		xCloseDisplay(dpy);
	}

	return EXIT_SUCCESS;
}
