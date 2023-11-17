#include "config.h"
#include "errors.h"
#include "util.h"

#include <assert.h>
#include <libgen.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N_COMPONENTS ((sizeof component_defns) / (sizeof(sbar_comp_defn_t)))

#define MAXLEN 128

typedef struct component {
	unsigned id;
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

static bool to_stdout = false;
static Display *dpy = NULL;

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

static void sbar_component_update(const component_t *c)
{
	char tmpbuf[MAXLEN];

	c->update(tmpbuf, MAXLEN, c->args, no_val_str);

	/*
	 * Maintain the status bar "dirty" invariant.
	 */
	Pthread_mutex_lock(&c->sbar->mutex);
	Memcpy(c->buf, tmpbuf, MAXLEN);
	c->sbar->dirty = true;
	Pthread_cond_signal(&c->sbar->dirty_cond);
	Pthread_mutex_unlock(&c->sbar->mutex);
}

static void *thread_flush(void *arg)
{
	sbar_t *sbar = (sbar_t *)arg;
	char status[N_COMPONENTS * MAXLEN];

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

static void *thread_repeating(void *arg)
{
	const component_t *c = (component_t *)arg;

	while (true) {
		Sleep(c->interval);
		sbar_component_update(c);
	}
	return NULL;
}

static void *thread_async(void *arg)
{
	component_t *c = (component_t *)arg;
	sigset_t sigset;
	int sig;

	Sigemptyset(&sigset);
	Sigaddset(&sigset, c->signum);

	while (true) {
		Sigwait(&sigset, &sig);
		assert(sig == c->signum && "unexpected signal received");
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

static void sbar_create(sbar_t *sbar, const uint8_t ncomponents,
			const sbar_comp_defn_t *comp_defns)
{
	component_t *cp;
	sigset_t sigset;

	sbar->component_bufs =
		Calloc(ncomponents * (size_t)MAXLEN, sizeof(char));
	sbar->ncomponents = ncomponents;
	sbar->components = Calloc(ncomponents, sizeof(component_t));
	sbar->dirty = false;
	Pthread_mutex_init(&sbar->mutex, NULL);
	Pthread_cond_init(&sbar->dirty_cond, NULL);

	/*
	 * The signal for which each asynchronous component thread will wait
	 * must be masked in all other threads.  This ensures that the signal
	 * will never be delivered to any other thread.  We set the mask here
	 * since all threads inherit their signal mask from their creator.
	 */
	Sigemptyset(&sigset);

	/* Create the components */
	for (unsigned i = 0; i < ncomponents; i++) {
		cp = &sbar->components[i];

		cp->id = i;
		cp->buf = sbar->component_bufs + ((size_t)MAXLEN * i);
		/* NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.strcpy) */
		strcpy(cp->buf, no_val_str);
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
			Sigaddset(&sigset, cp->signum);
		}

		cp->sbar = sbar;
	}

	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
}

static void sbar_start(sbar_t *sbar)
{
	pthread_attr_t attr;
	pthread_t tid;
	component_t *c;

	Pthread_attr_init(&attr);
	Pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	Pthread_create(&sbar->thread, &attr, thread_flush, sbar);

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
	char pidfile[MAXLEN];
	FILE *fp;
	sbar_t sbar;
	sigset_t sigset;
	int sig;

	/* NOLINTNEXTLINE(concurrency-mt-unsafe) */
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

	/* Save the pid to a file so it’s available to shell commands */
	Snprintf(pidfile, MAXLEN, "/tmp/%s.pid", basename(argv[0]));
	fp = Fopen(pidfile, "w");
	if (fprintf(fp, "%ld", (long)getpid()) < 0)
		app_error("Unable to create PID file");
	Fclose(fp);

	if (!to_stdout)
		dpy = xOpenDisplay(NULL);

	/*
	 * We want SIGINT and SIGTERM delivered only to the initial thread.  We
         * mask them here, since the mask will be inherited by new threads, and
         * unmask them after the threads have been created.
	 */
	Sigemptyset(&sigset);
	Sigaddset(&sigset, SIGINT);
	Sigaddset(&sigset, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	sbar_create(&sbar, N_COMPONENTS, component_defns);
	sbar_start(&sbar);

	Sigwait(&sigset, &sig);
	switch (sig) {
	case SIGINT:
		Fprintf(stdout, "SIGINT received. Terminating.\n");
		break;
	case SIGTERM:
		Fprintf(stdout, "SIGTERM received. Terminating.\n");
		break;
	default:
		Fprintf(stdout, "Unexpected signal received. Terminating.\n");
	}

	if (!to_stdout) {
		/* NOLINTNEXTLINE(clang-analyzer-core.NullDereference) */
		xStoreName(dpy, DefaultRootWindow(dpy), NULL);
		XCloseDisplay(dpy);
	}

	if (remove(pidfile) < 0)
		unix_warn("Unable to remove %s", pidfile);

	return EXIT_SUCCESS;
}
