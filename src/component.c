#define _POSIX_C_SOURCE 200809L

#include "component.h"

#include "util.h"

#include <X11/Xlib.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

comp_ret_t component_keyb_ind(char *buf, const size_t bufsize, const char *args)
{
	Display *dpy;
	XKeyboardState state;
	bool caps_on, numlock_on;
	char *val = "";

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		return (comp_ret_t){ false, "Unable to open display" };

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

comp_ret_t component_notmuch(char *buf, const size_t bufsize, const char *args)
{
	char cmdbuf[MAXLEN] = { 0 };
	char *const argv[] = { "notmuch", "count",
			       "tag:unread NOT tag:archived", NULL };
	long count;

	snprintf(buf, bufsize, " %s", NO_VAL_STR);

	if (util_run_cmd(cmdbuf, sizeof(cmdbuf), argv) != 0)
		return (comp_ret_t){ false, "Error running notmuch" };

	errno = 0; /* To distinguish success/failure after call */
	count = strtol(cmdbuf, NULL, 0);
	assert(!errno);

	snprintf(buf, bufsize, "%s %ld", (count ? "" : ""), count);

	return (comp_ret_t){ .ok = true };
}

comp_ret_t component_parse_meminfo(char *out, const size_t outsize, char *in,
				   const size_t insize)
{
	char *m, *s, *token, *saveptr;
	uintmax_t val;
	int i;

	in[insize - 1] = '\0';

	m = strstr(in, "MemAvailable");
	if (m == NULL)
		return (comp_ret_t){ false, "Unable to parse meminfo" };

	for (i = 0, s = m; i < 2; i++, s = NULL) {
		token = strtok_r(s, " ", &saveptr);
		if (token == NULL)
			return (comp_ret_t){ false, "Unable to parse meminfo" };
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
	if (!ret.ok)
		return ret;

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
