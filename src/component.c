#include <X11/Xlib.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>

int run_cmd(char *buf, const size_t bufsize, const char *cmd)
{
	char *p;
	FILE *fp;

	if ((fp = popen(cmd, "r")) == NULL) {
		return errno;
	}

	p = fgets(buf, bufsize - 1, fp);
	if (pclose(fp) < 0) {
		return errno;
	}
	if (!p)
		return errno;

	if ((p = strrchr(buf, '\n')))
		p[0] = '\0';

	return 0;
}

void component_keyb_ind(char *buf, const size_t bufsize, const char *args,
			const char *no_val_str)
{
	(void)args;
	(void)no_val_str;
	Display *dpy;
	XKeyboardState state;
	bool caps_on, numlock_on;
	char *val;

	if (!(dpy = XOpenDisplay(NULL))) {
		fputs("XOpenDisplay: Unable to open display", stderr);
		return;
	}
	XGetKeyboardControl(dpy, &state);
	XCloseDisplay(dpy);

	caps_on = state.led_mask & (1 << 0);
	numlock_on = state.led_mask & (1 << 1);
	if (caps_on && numlock_on)
		val = "Caps Num";
	else if (caps_on)
		val = "Caps";
	else if (numlock_on)
		val = "Num";
	else
		val = "";

	size_t len = strlen(val);
	assert(bufsize >= len + 1);
	memcpy(buf, val, len + 1);
}

void component_notmuch(char *buf, const size_t bufsize, const char *args,
		       const char *no_val_str)
{
	(void)args;
	char output[bufsize], errbuf[bufsize];
	long count;
	int r;

	snprintf(buf, bufsize, " %s", no_val_str);

	if ((r = run_cmd(output, bufsize,
			 "notmuch count 'tag:unread NOT tag:archived'")) != 0) {
		strerror_r(r, errbuf, sizeof(errbuf));
		fprintf(stderr, "notmuch command failed: %s", errbuf);
		return;
	}

	errno = 0; /* To distinguish success/failure after call */
	count = strtol(output, NULL, 0);
	if (errno != 0) {
		strerror_r(errno, errbuf, sizeof(errbuf));
		fprintf(stderr, "Unable to convert command output: %s", errbuf);
		return;
	}
	if (count == 0)
		snprintf(buf, bufsize, " %ld", count);
	else
		snprintf(buf, bufsize, " %ld", count);
}

void component_mem_avail(char *buf, const size_t bufsize, const char *args,
			 const char *no_val_str)
{
	(void)args;
	const char *meminfo = "/proc/meminfo";
	const char *target = "MemAvailable";
	FILE *fp;
	char line[bufsize], val_str[bufsize];
	char *ret, *token, *s, *saveptr;
	int i;
	uintmax_t val;

	snprintf(buf, bufsize, " %s", no_val_str);

	if ((fp = fopen(meminfo, "r")) == NULL) {
		fprintf(stderr, "Unable to open %s", meminfo);
		return;
	}

	while ((ret = fgets(line, bufsize, fp)) != NULL)
		if (strstr(line, target) != NULL)
			break;
	if (ret == NULL) { /* EOF reached */
		fprintf(stderr, "%s not found in %s", target, meminfo);
		fclose(fp);
		return;
	}

	for (i = 0, s = line; i < 2; i++, s = NULL)
		if ((token = strtok_r(s, " ", &saveptr)) == NULL) {
			fprintf(stderr, "Unable to parse line: %s", line);
			fclose(fp);
			return;
		}

	fclose(fp);

	if ((val = strtoumax(token, NULL, 0)) == 0 || val == INTMAX_MAX ||
	    val == UINTMAX_MAX) {
		fprintf(stderr, "Unable to convert value %s", token);
		return;
	}

	util_fmt_human(val_str, sizeof(val_str), val * K_IEC, K_IEC);
	snprintf(buf, bufsize, " %s", val_str);
}

void component_disk_free(char *buf, const size_t bufsize, const char *path,
			 const char *no_val_str)
{
	struct statvfs fs;
	char output[bufsize], errbuf[bufsize];
	int r;

	snprintf(buf, bufsize, "󰋊 %s", no_val_str);

	if ((r = statvfs(path, &fs)) < 0) {
		strerror_r(r, errbuf, sizeof(errbuf));
		fprintf(stderr, "statvfs: '%s': %s", path, errbuf);
		return;
	}

	util_fmt_human(output, sizeof(output), fs.f_frsize * fs.f_bavail, K_IEC);
	snprintf(buf, bufsize, "󰋊 %s", output);
}

void component_datetime(char *buf, const size_t bufsize, const char *date_fmt,
			const char *no_val_str)
{
	time_t t;
	struct tm now;
	char output[bufsize], errbuf[bufsize];

	snprintf(buf, bufsize, " %s", no_val_str);

	if ((t = time(NULL)) == -1) {
		strerror_r(errno, errbuf, sizeof(errbuf));
		fprintf(stderr, "time: %s", errbuf);
		return;
	}
	if (localtime_r(&t, &now) == NULL) {
		strerror_r(errno, errbuf, sizeof(errbuf));
		fprintf(stderr, "Unable to determine local time: %s", errbuf);
		return;
	}
	if (strftime(output, sizeof(output), date_fmt, &now) == 0)
		fputs("Unable to format time", stderr);
	snprintf(buf, bufsize, " %s", output);
}
