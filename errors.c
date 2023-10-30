/* Some standard error handling routines used by various programs.

   Based on the code from the book "The Linux Programming Interface" by Michael
   Kerrisk. */

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
 *   - outputting the caller-supplied error message specified in ‘format’ and
 *     ‘ap’.
 */
static void output_error(bool use_err, int err, const char *format, va_list ap)
{
	char user_msg[BUF_SIZE];

	(void)vsnprintf(user_msg, BUF_SIZE, format, ap);

	if (use_err)
		(void)fprintf(stderr, "ERROR -- %s: %s\n", user_msg,
			      strerror(err));
	else
		(void)fprintf(stderr, "ERROR -- %s\n", user_msg);
}

/*
 * Display error message including ‘errnum’ diagnostic, and return to caller.
 */
void err_msg_en(int errnum, const char *format, ...)
{
	va_list arg_list;

	va_start(arg_list, format);
	output_error(true, errnum, format, arg_list);
	va_end(arg_list);
}

/*
 * Display error message including ‘errno’ diagnostic, and return to caller.
 */
void err_msg(const char *format, ...)
{
	va_list arg_list;

	va_start(arg_list, format);
	output_error(true, errno, format, arg_list);
	va_end(arg_list);
}

/*
 * Display error message including ‘errnum’ diagnostic, and terminate the
 * process.
 */
void err_exit_en(int errnum, const char *format, ...)
{
	va_list arg_list;

	va_start(arg_list, format);
	output_error(true, errnum, format, arg_list);
	va_end(arg_list);

	exit(EXIT_FAILURE);
}

/*
 * Display error message including ‘errno’ diagnostic, and terminate the
 * process.
 */
void err_exit(const char *format, ...)
{
	va_list arg_list;

	va_start(arg_list, format);
	output_error(true, errno, format, arg_list);
	va_end(arg_list);

	exit(EXIT_FAILURE);
}

/*
 * Print an error message (without an 'errno' diagnostic).
 */
void fatal(const char *format, ...)
{
	va_list argList;

	va_start(argList, format);
	output_error(false, 0, format, argList);
	va_end(argList);

	exit(EXIT_FAILURE);
}
