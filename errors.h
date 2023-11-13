#ifndef ERRORS_H
#define ERRORS_H

#include <X11/Xlib.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Error-handling functions.
 */
void unix_warn(const char *fmt, ...);
void unix_error(const char *fmt, ...);
void posix_warn(int code, const char *fmt, ...);
void posix_error(int code, const char *fmt, ...);
void app_warn(const char *fmt, ...);
void app_error(const char *fmt, ...);

/*
 * Process control wrappers.
 */
unsigned int Sleep(unsigned int secs);
void Pause(void);

/*
 * Signal wrappers.
 */
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * Memory mapping wrappers.
 */
void *Memcpy(void *dest, const void *src, size_t n);

/*
 * Dynamic storage allocation wrappers.
 */
void *Malloc(size_t size);
void *Calloc(size_t nmemb, size_t size);
void Free(void *ptr);

/*
 * Standard I/O wrappers.
 */
FILE *Fopen(const char *filename, const char *mode);
void Fclose(FILE *fp);
void Fflush(FILE *fp);
void Puts(const char *s);
int Snprintf(char *str, size_t size, const char *fmt, ...);

/*
 * String wrappers.
 */
char *Strcpy(char dst[], const char *src);

/*
 * Pthreads thread control wrappers.
 */
void Pthread_create(pthread_t *tidp, pthread_attr_t *attrp,
		    void *(*routine)(void *), void *argp);
void Pthread_detach(pthread_t tid);
pthread_t Pthread_self(void);
void Pthread_mutex_init(pthread_mutex_t *mutex,
			const pthread_mutexattr_t *attr);
void Pthread_mutex_lock(pthread_mutex_t *mutex);
void Pthread_mutex_unlock(pthread_mutex_t *mutex);
void Pthread_mutex_destroy(pthread_mutex_t *mutex);
void Pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr);
void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
void Pthread_cond_signal(pthread_cond_t *cond);
void Pthread_cond_destroy(pthread_cond_t *cond);

/*
 * XLIB wrappers.
 */
Display *xOpenDisplay(_Xconst char *display_name);
void xStoreName(Display *display, Window w, _Xconst char *window_name);
void xCloseDisplay(Display *display);
void xFlush(Display *display);

#endif
