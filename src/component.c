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

#define MAXLEN 128

typedef struct {
	bool ok;
	char message[MAXLEN];
} comp_ret_t;

int run_cmd(char *buf, const size_t bufsize, const char *cmd)
{
	char *p;
	FILE *fp;
	int r;

	fp = popen(cmd, "r");
	if (fp == NULL)
		return errno;

	p = fgets(buf, (int)bufsize - 1, fp);
	r = pclose(fp);
	if (!p || r < 0)
		return errno;

	p = strrchr(buf, '\n');
	if (p)
		p[0] = '\0';

	return 0;
}

comp_ret_t component_keyb_ind(char *buf, const size_t bufsize, const char *args,
			      const char *no_val_str)
{
	(void)args;
	(void)no_val_str;
	Display *dpy;
	XKeyboardState state;
	bool caps_on, numlock_on;
	char *val = "";

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		return (comp_ret_t){ .ok = false,
				     .message = "Unable to open display" };

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

	size_t len = strlen(val);
	assert(bufsize >= len + 1);
	memcpy(buf, val, len + 1);

	return (comp_ret_t){ .ok = true };
}

comp_ret_t component_notmuch(char *buf, const size_t bufsize, const char *args,
			     const char *no_val_str)
{
	(void)args;
	char output[bufsize], errbuf[bufsize];
	long count;
	int r;
	comp_ret_t ret;

	snprintf(buf, bufsize, " %s", no_val_str);

	r = run_cmd(output, bufsize,
		    "notmuch count 'tag:unread NOT tag:archived'");
	if (r != 0) {
		ret.ok = false;
		strerror_r(r, errbuf, sizeof(errbuf));
		snprintf(ret.message, sizeof(ret.message),
			 "notmuch command failed: %s", errbuf);
		return ret;
	}

	errno = 0; /* To distinguish success/failure after call */
	count = strtol(output, NULL, 0);
	if (errno != 0) {
		ret.ok = false;
		strerror_r(errno, errbuf, sizeof(errbuf));
		snprintf(ret.message, sizeof(ret.message),
			 "Unable to convert command output: %s", errbuf);
		return ret;
	}
	if (count == 0)
		snprintf(buf, bufsize, " %ld", count);
	else
		snprintf(buf, bufsize, " %ld", count);

	ret.ok = true;
	return ret;
}

comp_ret_t component_mem_avail(char *buf, const size_t bufsize,
			       const char *args, const char *no_val_str)
{
	(void)args;
	const char *meminfo = "/proc/meminfo";
	const char *target = "MemAvailable";
	FILE *fp;
	char line[bufsize], val_str[bufsize];
	char *r, *token, *s, *saveptr;
	int i;
	uintmax_t val;
	comp_ret_t ret;

	snprintf(buf, bufsize, " %s", no_val_str);

	if ((fp = fopen(meminfo, "r")) == NULL) {
		ret.ok = false;
		snprintf(ret.message, sizeof(ret.message), "Unable to open %s",
			 meminfo);
		return ret;
	}

	while ((r = fgets(line, (int)bufsize, fp)) != NULL)
		if (strstr(line, target) != NULL)
			break;
	if (r == NULL) { /* EOF reached */
		fclose(fp);
		ret.ok = false;
		snprintf(ret.message, sizeof(ret.message), "%s not found in %s",
			 target, meminfo);
		return ret;
	}

	for (i = 0, s = line; i < 2; i++, s = NULL)
		if ((token = strtok_r(s, " ", &saveptr)) == NULL) {
			fclose(fp);
			ret.ok = false;
			snprintf(ret.message, sizeof(ret.message),
				 "Unable to parse line: %s", line);
			return ret;
		}

	fclose(fp);

	if ((val = strtoumax(token, NULL, 0)) == 0 || val == INTMAX_MAX ||
	    val == UINTMAX_MAX) {
		ret.ok = false;
		snprintf(ret.message, sizeof(ret.message),
			 "Unable to convert value %s", token);
		return ret;
	}

	util_fmt_human(val_str, sizeof(val_str), val * K_IEC, K_IEC);
	snprintf(buf, bufsize, " %s", val_str);
	ret.ok = true;
	return ret;
}

comp_ret_t component_disk_free(char *buf, const size_t bufsize,
			       const char *path, const char *no_val_str)
{
	struct statvfs fs;
	char output[bufsize], errbuf[bufsize];
	int r;
	comp_ret_t ret;

	snprintf(buf, bufsize, "󰋊 %s", no_val_str);

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

comp_ret_t component_datetime(char *buf, const size_t bufsize,
			      const char *date_fmt, const char *no_val_str)
{
	time_t t;
	struct tm now;
	char output[bufsize], errbuf[bufsize];
	comp_ret_t ret;

	snprintf(buf, bufsize, " %s", no_val_str);

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
		return (comp_ret_t){ .ok = false,
				     .message = "Unable to format time" };
	}
	snprintf(buf, bufsize, " %s", output);
	ret.ok = true;
	return ret;
}
