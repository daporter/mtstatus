#include "util.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

bool util_file_get_line(char **buffer, size_t *restrict buffer_len,
			const char *restrict target, const char *restrict path)
{
	FILE *restrict file;
	bool return_val = false;

	file = fopen(path, "r");
	if (!file) {
		char msg[64];
		sprintf(msg, "Error: unable to open '%s'", path);
		perror(msg);
		return false;
	}

	while (getline(buffer, buffer_len, file) != -1) {
		*(strchrnul(*buffer, '\n')) = 0;  // remove trailing newline
		if (strstr(*buffer, target) != NULL) {
			return_val = true;
			break;
		}
	}

	fclose(file);
	return return_val;
}

bool util_string_get_nth_field(char *buffer, size_t buffer_size, char *string,
			       const int n)
{
	char *token;
	size_t token_len;
	int count = 1;
	char *saveptr;

	if (!string || n <= 0)
		return NULL;

	token = strtok_r(string, " ", &saveptr);
	while (token != NULL && count < n) {
		token = strtok_r(NULL, " ", &saveptr);
		count++;
	}
	if (count < n)
		return false;

	token_len = strlen(token);
	assert(buffer_size >= token_len + 1);
	memmove(buffer, token, token_len + 1);
	return true;
}

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

	int ret, status;
	pid_t pid;
	ssize_t nread;
	char argv_s[bufsize];
	int pipefd[2];

	ret = pipe(pipefd);
	if (ret == -1) {
		log_errno(errno, "Error creating pipe");
		return false;
	}

	pid = fork();
	switch (pid) {
	case -1:
		log_errno(errno, "Error: unable to fork");
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	case 0:
		ret = dup2(pipefd[1], 1);
		if (ret < 0) {
			log_errno(errno,
				  "Error: unable to dup2 in child process");
			_exit(EXIT_FAILURE);
		}
		execvp(argv[0], argv);
		_exit(EXIT_FAILURE);  // Failed exec
	default:
		nread = read(pipefd[0], buf, bufsize);
		assert(nread != -1);
		buf[nread - 1] = '\0';	// Remove trailing newline
		ret = waitpid(pid, &status, 0);
		assert(ret != -1);
		close(pipefd[0]);
		close(pipefd[1]);
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
	}

	return true;
}

void log_err(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	(void)fputc('\n', stderr);
	va_end(ap);
}

void log_errno(int errnum, const char *fmt, ...)
{
	char msg[1024], err[1024];  // size recommended by "man strerror_r"

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(msg, sizeof(msg), fmt, ap);
	assert(n >= 0 && (size_t)n < sizeof(msg));
	va_end(ap);

	strerror_r(errnum, err, sizeof(err));
	log_err("%s: %s", msg, err);
}
