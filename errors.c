/*
 * Some standard error handling routines used by various programs.
 *
 * Based on the code from the book "The Linux Programming Interface" by Michael
 * Kerrisk.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 500

/*
 * Diagnose ‘errno’ error by:
 *
 *   - outputting the corresponding error message from strerror(), and
 *
 *   - outputting the caller-supplied error message specified in ‘fmt’ and ‘ap’.
 */
static void verr(bool use_err, int err, const char *fmt, va_list ap)
{
	char msg[BUF_SIZE];

	(void)vsnprintf(msg, BUF_SIZE, fmt, ap);

	if (use_err)
		(void)fprintf(stderr, "ERROR -- %s: %s\n", msg, strerror(err));
	else
		(void)fprintf(stderr, "ERROR -- %s\n", msg);
}

/*
 * Display error message including ‘errnum’ diagnostic, and return to caller.
 */
void warn_errnum(int errnum, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(true, errnum, fmt, ap);
	va_end(ap);
}

/*
 * Display error message including ‘errno’ diagnostic, and return to caller.
 */
void warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(true, errno, fmt, ap);
	va_end(ap);
}

/*
 * Display error message including ‘errnum’ diagnostic, and terminate the
 * process.
 */
void die_errnum(int errnum, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(true, errnum, fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

/*
 * Display error message including ‘errno’ diagnostic, and terminate the
 * process.
 */
void die_errno(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(true, errno, fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(false, 0, fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}
