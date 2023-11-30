#include "util.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
