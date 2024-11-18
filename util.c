#include "util.h"

#include "mtstatus.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char *util_cat(char *dest, const char *end, const char *str)
{
	while (dest < end && *str)
		*dest++ = *str++;
	return dest;
}

int util_fmt_human(char *buf, size_t len, uintmax_t num, int base)
{
	double scaled;
	size_t prefixlen;
	uint8_t i;
	const char **prefix;
	const char *prefix_si[] = { "", "k", "M", "G", "T",
                                    "P", "E", "Z", "Y" };
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

	return snprintf(buf, len, "%4.3g %s", scaled, prefix[i]);
}

static void argv_str(char *buf, const size_t bufsize, char *const argv[])
{
	char *p = buf;
	char *end = buf + bufsize;
	if (argv[0]) {
		p = util_cat(p, end, argv[0]);
		for (int i = 1; argv[i] && p < end; ++i) {
			p = util_cat(p, end, " ");
			p = util_cat(p, end, argv[i]);
		}
	}
	*p = '\0';
}

bool util_run_cmd(char *buf, const size_t bufsize, char *const argv[])
{
	assert(argv[0] && "argv[0] must not be NULL");

	int pipefd[2];
	int r = pipe(pipefd);
	if (r == -1) {
		log_errno(errno, "Error creating pipe");
		return false;
	}

	ssize_t nread;
	int s, status;
	pid_t pid = fork();
	switch (pid) {
	case -1:
		log_errno(errno, "Error: unable to fork");
		return false;
	case 0:
		s = dup2(pipefd[1], 1);
		assert(s != -1);
		execvp(argv[0], argv);
		_exit(EXIT_FAILURE);  // Failed exec
	default:
		nread = read(pipefd[0], buf, bufsize);
		assert(nread != -1);
		buf[nread - 1] = '\0';	// Remove trailing newline
		s = waitpid(pid, &status, 0);
		assert(s != -1);
		char argv_s[MAXLEN];
		argv_str(argv_s, sizeof(argv_s), argv);
		if (!WIFEXITED(status)) {
			log_err("Error: command terminated abnormally: '%s'",
				argv_s);
			return false;
		}
		if (WEXITSTATUS(status)) {
			log_err("Error: command exited with status %d: '%s'",
				status, argv_s);
			return false;
		}
		close(pipefd[0]);
		close(pipefd[1]);
	}
	return true;
}
