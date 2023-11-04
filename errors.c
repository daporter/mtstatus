/* Error-handling functions */

#include "errors.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 500

/* Some standard error handling routines used by various programs */

static void verr(bool use_err, int err, const char *fmt, va_list ap)
{
	char msg[BUF_SIZE];

	(void)vsnprintf(msg, BUF_SIZE, fmt, ap);

	if (use_err)
		(void)fprintf(stderr, "ERROR -- %s: %s\n", msg, strerror(err));
	else
		(void)fprintf(stderr, "ERROR -- %s\n", msg);
}

/* Unix-style warning */
void unix_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(true, errno, fmt, ap);
	va_end(ap);
}

/* Unix-style error */
void unix_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(true, errno, fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

/* Posix-style warning */
void posix_warn(int code, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(true, code, fmt, ap);
	va_end(ap);
}

/* Posix-style error */
void posix_error(int code, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(true, code, fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

/* Application-style warning */
void app_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(false, 0, fmt, ap);
	va_end(ap);
}

/* Application error */
void app_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(false, 0, fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

/* Wrappers for Unix process control functions */

unsigned int Sleep(unsigned int secs)
{
	return sleep(secs);
}

void Pause(void)
{
	(void)pause();
}

/* Wrappers for Unix signal functions */

handler_t *Signal(int signum, handler_t *handler)
{
	struct sigaction action, old_action;

	action.sa_handler = handler;
	sigemptyset(&action.sa_mask); /* Block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* Restart syscalls if possible */

	if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
	return (old_action.sa_handler);
}

/* Memory mapping wrappers */
void *Memcpy(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

/* Wrappers for dynamic storage allocation functions */

void *Malloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		unix_error("Malloc error");
	return p;
}

void *Calloc(size_t nmemb, size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		unix_error("Calloc error");
	return p;
}

void Free(void *ptr)
{
	free(ptr);
}

/* Wrappers for the Standard I/O functions */

FILE *Fopen(const char *filename, const char *mode)
{
	FILE *fp;

	if ((fp = fopen(filename, mode)) == NULL)
		unix_error("Fopen error");

	return fp;
}

void Fclose(FILE *fp)
{
	if (fclose(fp) != 0)
		unix_error("Fclose error");
}

void Fflush(FILE *fp)
{
	if (fflush(fp) == EOF)
		unix_error("Fclose error");
}

void Puts(const char *s)
{
	if (puts(s) == EOF)
		unix_error("Puts error");
}

int Snprintf(char *str, size_t size, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vsnprintf(str, size, fmt, ap);
	va_end(ap);

	if (ret < 0) {
		unix_warn("vsnprintf");
		return -1;
	}
	if ((size_t)ret >= size) {
		app_warn("vsnprintf: Output truncated");
		return -1;
	}

	return ret;
}

/* Wrappers for Pthreads thread control functions */

void Pthread_create(pthread_t *tidp, pthread_attr_t *attrp,
		    void *(*routine)(void *), void *argp)
{
	int rc;

	if ((rc = pthread_create(tidp, attrp, routine, argp)) != 0)
		posix_error(rc, "Pthread_create error");
}

void Pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	int rc;

	if ((rc = pthread_mutex_init(mutex, attr)) != 0)
		posix_error(rc, "Pthread_mutex_init");
}

void Pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int rc;

	if ((rc = pthread_mutex_lock(mutex)) != 0)
		posix_error(rc, "Pthread_mutex_lock");
}

void Pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	int rc;

	if ((rc = pthread_mutex_unlock(mutex)) != 0)
		posix_error(rc, "Pthread_mutex_unlock");
}

void Pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr)
{
	int rc;

	if ((rc = pthread_cond_init(cond, attr)) != 0)
		posix_error(rc, "Pthread_cond_init");
}

void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	int rc;

	if ((rc = pthread_cond_wait(cond, mutex)) != 0)
		posix_error(rc, "Pthread_cond_wait");
}

void Pthread_cond_signal(pthread_cond_t *cond)
{
	int rc;

	if ((rc = pthread_cond_signal(cond)) != 0)
		posix_error(rc, "Pthread_cond_signal");
}

void Pthread_detach(pthread_t tid)
{
	int rc;

	if ((rc = pthread_detach(tid)) != 0)
		posix_error(rc, "Pthread_detach error");
}

pthread_t Pthread_self(void)
{
	return pthread_self();
}

/* Wrappers for XLIB functions */

Display *xOpenDisplay(_Xconst char *display_name)
{
	Display *display;

	if ((display = XOpenDisplay(display_name)) == NULL)
		unix_error("XOpenDisplay");

	return display;
}

void xStoreName(Display *display, Window w, _Xconst char *window_name)
{
	if (XStoreName(display, w, window_name) == 0)
		unix_error("XStoreName");
}

void xCloseDisplay(Display *display)
{
	if (XCloseDisplay(display) == 0)
		unix_error("XCloseDisplay");
}

void xFlush(Display *display)
{
	if (XFlush(display) == 0)
		unix_error("XFlush");
}
