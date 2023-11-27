#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#define DIVIDER	   "  "
#define NO_VAL_STR "n/a"

#define K_SI  1000
#define K_IEC 1024

#define LEN(x) (sizeof(x) / sizeof((x)[0]))

#define N_COMPONENTS ((sizeof component_defns) / (sizeof(sbar_comp_defn_t)))

#define MAXLEN 128

typedef struct comp_ret comp_ret_t;

struct comp_ret {
	bool ok;
	char message[MAXLEN];
};

/*
 * Function that returns an updated value for a status bar component.
 */
typedef comp_ret_t (*sbar_updater_t)(char *buf, const size_t bufsize,
				     const char *args);

typedef struct sbar_comp_defn sbar_comp_defn_t;

struct sbar_comp_defn {
	const sbar_updater_t update;
	const char *args;
	const time_t interval;
	const int signum;
};

typedef struct sbar_comp sbar_comp_t;

struct sbar_comp {
	unsigned id;
	char *buf;
	sbar_updater_t update;
	const char *args;
	time_t interval;
	int signum;
	pthread_t thr_repeating;
	pthread_t thr_async;
	struct sbar *sbar;
};

typedef struct sbar sbar_t;

struct sbar {
	char *comp_bufs;
	uint8_t ncomponents;
	sbar_comp_t *components;
	bool dirty;
	pthread_mutex_t mutex;
	pthread_cond_t dirty_cond;
	pthread_t thread;
};

comp_ret_t component_keyb_ind(char *buf, size_t bufsize, const char *args);
comp_ret_t component_notmuch(char *buf, size_t bufsize, const char *args);
comp_ret_t component_mem_avail(char *buf, size_t bufsize, const char *args);
comp_ret_t component_disk_free(char *buf, size_t bufsize, const char *path);
comp_ret_t component_volume(char *buf, size_t bufsize, const char *path);
comp_ret_t component_datetime(char *buf, size_t bufsize, const char *date_fmt);

/* clang-format off */
const sbar_comp_defn_t component_defns[] = {
	/* /\* function,        arguments,     interval, signal (SIGRTMIN+n) *\/ */
	{ component_keyb_ind,              0,  -1,        0 },
	{ component_notmuch,               0,  -1,        1 },
	/* network traffic */
	/* cpu */
	{ component_mem_avail,             0,   2,       -1 },
	{ component_disk_free,           "/",  15,       -1 },
	{ component_volume,                0,  -1,        2 },
	/* wifi network */
	{ component_datetime,  "%a %d %b %R",  30,       -1 },
};
/* clang-format on */

char pidfile[MAXLEN];
bool to_stdout = false;
Display *dpy = NULL;

static char *util_cat(char *dest, const char *end, const char *str)
{
	while (dest < end && *str)
		*dest++ = *str++;
	return dest;
}

static int util_fmt_human(char *buf, size_t len, uintmax_t num, int base)
{
	double scaled;
	size_t prefixlen;
	uint8_t i;
	const char **prefix;
	const char *prefix_si[] = { "", "k", "M", "G", "T", "P", "E", "Z", "Y" };
	const char *prefix_iec[] = { "",   "Ki", "Mi", "Gi", "Ti",
				     "Pi", "Ei", "Zi", "Yi" };

	switch (base) {
	case K_SI:
		prefix = prefix_si;
		prefixlen = LEN(prefix_si);
		break;
	case K_IEC:
		prefix = prefix_iec;
		prefixlen = LEN(prefix_iec);
		break;
	default:
		return -1;
	}

	scaled = (double)num;
	for (i = 0; i < prefixlen && scaled >= base; i++)
		scaled /= base;

	return snprintf(buf, len, "%.1f %s", scaled, prefix[i]);
}

static int util_run_cmd(char *buf, const size_t bufsize, char *const argv[])
{
	int pipefd[2];
	pid_t pid;
	int status;
	ssize_t nread;

	assert(argv[0] && "argv[0] must not be NULL");

	if (pipe(pipefd) == -1) {
		/* TODO: return a more informative value? */
		return EXIT_FAILURE;
	}

	pid = fork();
	switch (pid) {
	case -1:
		/* TODO: return a more informative value? */
		return EXIT_FAILURE;
	case 0:
		status = dup2(pipefd[1], 1);
		assert(status != -1 && "dup2 used incorrectly");
		execvp(argv[0], argv);
		_exit(EXIT_FAILURE); /* Failed exec */
	default:
		nread = read(pipefd[0], buf, bufsize);
		assert(nread != -1 && "read used incorrectly");
		buf[nread - 1] = '\0'; /* Remove trailing newline */
		if (waitpid(pid, &status, 0) == -1) {
			/* TODO: return a more informative value? */
			return EXIT_FAILURE;
		}
		close(pipefd[0]);
		close(pipefd[1]);
	}
	return EXIT_SUCCESS;
}

comp_ret_t component_keyb_ind(char *buf, const size_t bufsize, const char *args)
{
	XKeyboardState state;
	bool caps_on, numlock_on;
	char *val = "";

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		return (comp_ret_t){ false, "Unable to open display" };
	}

	XGetKeyboardControl(dpy, &state);

	caps_on = state.led_mask & (1 << 0);
	numlock_on = state.led_mask & (1 << 1);
	if (caps_on && numlock_on) {
		val = "Caps Num";
	} else if (caps_on) {
		val = "Caps";
	} else if (numlock_on) {
		val = "Num";
	}

	size_t len = strlen(val);
	assert(bufsize >= len + 1);
	memcpy(buf, val, len + 1);

	return (comp_ret_t){ .ok = true };
}

comp_ret_t component_notmuch(char *buf, const size_t bufsize, const char *args)
{
	char cmdbuf[MAXLEN] = { 0 };
	char *const argv[] = { "notmuch", "count",
			       "tag:unread NOT tag:archived", NULL };
	long count;

	snprintf(buf, bufsize, " %s", NO_VAL_STR);

	if (util_run_cmd(cmdbuf, sizeof(cmdbuf), argv) != 0) {
		return (comp_ret_t){ false, "Error running notmuch" };
	}
	errno = 0; /* To distinguish success/failure after call */
	count = strtol(cmdbuf, NULL, 0);
	assert(!errno);

	snprintf(buf, bufsize, "%s %ld", (count ? "" : ""), count);

	return (comp_ret_t){ .ok = true };
}

static comp_ret_t component_parse_meminfo(char *out, const size_t outsize,
					  char *in, const size_t insize)
{
	char *m, *s, *token, *saveptr;
	uintmax_t val;
	int i;

	in[insize - 1] = '\0';

	m = strstr(in, "MemAvailable");
	if (m == NULL) {
		return (comp_ret_t){ false, "Unable to parse meminfo" };
	}
	for (i = 0, s = m; i < 2; i++, s = NULL) {
		token = strtok_r(s, " ", &saveptr);
		if (token == NULL) {
			return (comp_ret_t){ false, "Unable to parse meminfo" };
		}
	}
	val = strtoumax(token, NULL, 0);
	if (val == 0 || val == INTMAX_MAX || val == UINTMAX_MAX) {
		comp_ret_t ret;
		ret.ok = false;
		snprintf(ret.message, sizeof(ret.message),
			 "Unable to convert value %s", token);
		return ret;
	}

	util_fmt_human(out, outsize, val * K_IEC, K_IEC);
	return (comp_ret_t){ .ok = true };
}

comp_ret_t component_mem_avail(char *buf, const size_t bufsize,
			       const char *args)
{
	FILE *f;
	char *meminfo = NULL;
	size_t len;
	ssize_t nread;
	char val_str[bufsize];
	comp_ret_t ret;

	snprintf(buf, bufsize, " %s", NO_VAL_STR);

	f = fopen("/proc/meminfo", "r");
	if (f == NULL)
		return (comp_ret_t){ false, "Error opening /proc/meminfo" };
	nread = getdelim(&meminfo, &len, '\0', f);
	if (nread == -1) {
		free(meminfo);
		fclose(f);
		return (comp_ret_t){ false, "Error reading /proc/meminfo" };
	}
	ret = component_parse_meminfo(val_str, bufsize, meminfo, nread);
	free(meminfo);
	fclose(f);
	if (!ret.ok) {
		return ret;
	}

	snprintf(buf, bufsize, " %s", val_str);
	ret.ok = true;
	return ret;
}

comp_ret_t component_disk_free(char *buf, const size_t bufsize,
			       const char *path)
{
	struct statvfs fs;
	char output[bufsize], errbuf[bufsize];
	int r;
	comp_ret_t ret;

	snprintf(buf, bufsize, "󰋊 %s", NO_VAL_STR);

	r = statvfs(path, &fs);
	if (r < 0) {
		ret.ok = false;
		strerror_r(r, errbuf, sizeof(errbuf));
		snprintf(ret.message, sizeof(ret.message), "statvfs: '%s': %s",
			 path, errbuf);
		return ret;
	}

	util_fmt_human(output, sizeof(output), fs.f_frsize * fs.f_bavail,
		       K_IEC);
	snprintf(buf, bufsize, "󰋊 %s", output);
	ret.ok = true;
	return ret;
}

comp_ret_t component_volume(char *buf, const size_t bufsize, const char *path)
{
	char cmdbuf[MAXLEN] = { 0 };
	char *const argv[] = { "pamixer", "--get-volume-human", NULL };

	snprintf(buf, bufsize, "󰝟 %s", NO_VAL_STR);

	if (util_run_cmd(cmdbuf, sizeof(cmdbuf), argv) != 0) {
		return (comp_ret_t){ false, "Error running pamixer" };
	}

	snprintf(buf, bufsize, "󰕾 %s", cmdbuf);
	return (comp_ret_t){ .ok = true };
}

comp_ret_t component_datetime(char *buf, const size_t bufsize,
			      const char *date_fmt)
{
	time_t t;
	struct tm now;
	char output[bufsize], errbuf[bufsize];
	comp_ret_t ret;

	snprintf(buf, bufsize, " %s", NO_VAL_STR);

	t = time(NULL);
	if (t == -1) {
		ret.ok = false;
		strerror_r(errno, errbuf, sizeof(errbuf));
		snprintf(ret.message, sizeof(ret.message), "time: %s", errbuf);
		return ret;
	}
	if (localtime_r(&t, &now) == NULL) {
		ret.ok = false;
		strerror_r(errno, errbuf, sizeof(errbuf));
		snprintf(ret.message, sizeof(ret.message),
			 "Unable to determine local time: %s", errbuf);
		return ret;
	}
	if (strftime(output, sizeof(output), date_fmt, &now) == 0) {
		return (comp_ret_t){ false, "Unable to format time" };
	}
	snprintf(buf, bufsize, " %s", output);
	ret.ok = true;
	return ret;
}

static void fatal(int code)
{
	fprintf(stderr, "mtstatus: fatal: %s\n", strerror(code));
	if (!to_stdout) {
		if (remove(pidfile) < 0) {
			fprintf(stderr, "Unable to remove %s", pidfile);
		}
	}
	exit(EXIT_FAILURE);
}

static void sbar_flush_on_dirty(sbar_t *sbar, char *buf, const size_t bufsize)
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
	assert(r == 0);
	while (!sbar->dirty) {
		r = pthread_cond_wait(&sbar->dirty_cond, &sbar->mutex);
		assert(r == 0);
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
		if (strlen(cbuf) > 0) {
			ptr = util_cat(ptr, end, cbuf);
		}
	}
	*ptr = '\0';

	sbar->dirty = false;
	r = pthread_mutex_unlock(&sbar->mutex);
	assert(r == 0);
}

static void sbar_component_update(const sbar_comp_t *c)
{
	char tmpbuf[MAXLEN];
	int r;
	comp_ret_t ret;

	ret = c->update(tmpbuf, sizeof(tmpbuf), c->args);
	if (!ret.ok) {
		fprintf(stderr, "Error updating component: %s\n", ret.message);
	}

	/*
	 * Maintain the status bar "dirty" invariant.
	 */
	r = pthread_mutex_lock(&c->sbar->mutex);
	assert(r == 0);
	static_assert(MAXLEN >= sizeof(tmpbuf),
		      "size of component buffer < sizeof(tmpbuf)");
	memcpy(c->buf, tmpbuf, sizeof(tmpbuf));
	c->sbar->dirty = true;
	r = pthread_cond_signal(&c->sbar->dirty_cond);
	assert(r == 0);
	r = pthread_mutex_unlock(&c->sbar->mutex);
	assert(r == 0);
}

static void *thread_flush(void *arg)
{
	sbar_t *sbar = (sbar_t *)arg;
	char status[N_COMPONENTS * MAXLEN];

	while (true) {
		sbar_flush_on_dirty(sbar, status, LEN(status));

		if (to_stdout) {
			if (puts(status) == EOF) {
				fatal(errno);
			}
			if (fflush(stdout) == EOF) {
				fatal(errno);
			}
		} else {
			XStoreName(dpy, DefaultRootWindow(dpy), status);
			XFlush(dpy);
		}
	}

	return NULL;
}

static void *thread_repeating(void *arg)
{
	const sbar_comp_t *c = (sbar_comp_t *)arg;

	while (true) {
		sleep((unsigned)c->interval);
		sbar_component_update(c);
	}
	return NULL;
}

static void *thread_async(void *arg)
{
	sbar_comp_t *c = (sbar_comp_t *)arg;
	sigset_t sigset;
	int sig, r;

	if (sigemptyset(&sigset) < 0) {
		fatal(errno);
	}
	if (sigaddset(&sigset, c->signum) < 0) {
		fatal(errno);
	}

	while (true) {
		r = sigwait(&sigset, &sig);
		if (r == -1) {
			fatal(r);
		}
		assert(sig == c->signum && "unexpected signal received");
		sbar_component_update(c);
	}

	return NULL;
}

static void *thread_once(void *arg)
{
	const sbar_comp_t *c = (sbar_comp_t *)arg;
	sbar_component_update(c);
	return NULL;
}

static void sbar_create(sbar_t *sbar, const uint8_t ncomponents,
			const sbar_comp_defn_t *comp_defns)
{
	sbar_comp_t *cp;
	sigset_t sigset;
	int r;

	sbar->comp_bufs = calloc(ncomponents * (size_t)MAXLEN, sizeof(char));
	if (sbar->comp_bufs == NULL) {
		fatal(errno);
	}
	sbar->ncomponents = ncomponents;
	sbar->components = calloc(ncomponents, sizeof(sbar_comp_t));
	if (sbar->components == NULL) {
		fatal(errno);
	}
	sbar->dirty = false;
	r = pthread_mutex_init(&sbar->mutex, NULL);
	if (r != 0) {
		fatal(r);
	}
	r = pthread_cond_init(&sbar->dirty_cond, NULL);
	if (r != 0) {
		fatal(r);
	}

	/*
	 * The signal for which each asynchronous component thread will wait
	 * must be masked in all other threads.  This ensures that the signal
	 * will never be delivered to any other thread.  We set the mask here
	 * since all threads inherit their signal mask from their creator.
	 */
	if (sigemptyset(&sigset) < 0) {
		fatal(errno);
	}

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
			if (sigaddset(&sigset, cp->signum) < 0) {
				fatal(errno);
			}
		}

		cp->sbar = sbar;
	}

	r = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if (r != 0) {
		fatal(r);
	}
}

static void sbar_start(sbar_t *sbar)
{
	pthread_attr_t attr;
	pthread_t tid;
	sbar_comp_t *c;
	int r;

	r = pthread_attr_init(&attr);
	if (r != 0) {
		fatal(r);
	}
	r = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (r != 0) {
		fatal(r);
	}
	r = pthread_create(&sbar->thread, &attr, thread_flush, sbar);
	if (r != 0) {
		fatal(r);
	}

	for (uint8_t i = 0; i < sbar->ncomponents; i++) {
		c = &sbar->components[i];

		r = pthread_create(&tid, &attr, thread_once, c);
		if (r != 0) {
			fatal(r);
		}

		if (c->interval >= 0) {
			r = pthread_create(&c->thr_repeating, &attr,
					   thread_repeating, c);
			if (r != 0) {
				fatal(r);
			}
		}
		if (c->signum >= 0) {
			r = pthread_create(&c->thr_async, &attr, thread_async,
					   c);
			if (r != 0) {
				fatal(r);
			}
		}
	}
}

static void usage(FILE *f)
{
	assert(f != NULL);
	fputs("Usage: mtstatus [-h] [-s]\n", f);
	fputs("  -h        Print this help message and exit\n", f);
	fputs("  -s        Output to stdout\n", f);
}

int main(int argc, char *argv[])
{
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
		if (f == NULL) {
			fatal(errno);
		}
		if (fprintf(f, "%ld", (long)getpid()) < 0) {
			fatal(errno);
		}
		fclose(f);

		dpy = XOpenDisplay(NULL);
		if (dpy == NULL) {
			fatal(errno);
		}
	}

	/* SIGINT and SIGTERM must be delivered only to the initial thread */
	sigset_t sigset;
	if (sigemptyset(&sigset) < 0) {
		fatal(errno);
	}
	if (sigaddset(&sigset, SIGINT) < 0) {
		fatal(errno);
	}
	if (sigaddset(&sigset, SIGTERM) < 0) {
		fatal(errno);
	}
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	/* Start the status bar */
	sbar_create(&sbar, N_COMPONENTS, component_defns);
	sbar_start(&sbar);

	/* Wait for SIGINT, SIGTERM */
	int sig, r;
	r = sigwait(&sigset, &sig);
	if (r == -1) {
		fatal(r);
	}

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

		if (remove(pidfile) < 0) {
			fprintf(stderr, "Unable to remove %s", pidfile);
		}
	}
}
