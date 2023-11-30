#include "util.h"

#include <X11/Xlib.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define NO_VAL_STR "n/a"

#define MSG_LEN 128

typedef struct comp_ret comp_ret_t;

struct comp_ret {
	bool ok;
	char message[MSG_LEN];
};

pthread_mutex_t cpu_data_mtx = PTHREAD_MUTEX_INITIALIZER;

static comp_ret_t comp_keyb_ind(char *buf, const size_t bufsize,
				const char *args)
{
	XKeyboardState state;
	bool caps_on, numlock_on;
	char *val = "";

	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) {
		return (comp_ret_t){ false, "Unable to open display" };
	}

	XGetKeyboardControl(dpy, &state);

	caps_on = state.led_mask & (1 << 0);
	numlock_on = state.led_mask & (1 << 1);
	if (caps_on && numlock_on) {
		val = "Caps Num";
	} else if (caps_on) {
		val = "Caps";
	} else if (numlock_on) {
		val = "Num";
	}

	size_t len = strlen(val);
	assert(bufsize >= len + 1);
	memcpy(buf, val, len + 1);

	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_notmuch(char *buf, const size_t bufsize,
			       const char *args)
{
	char cmdbuf[MSG_LEN] = { 0 };
	char *const argv[] = { "notmuch", "count",
			       "tag:unread NOT tag:archived", NULL };
	long count;

	snprintf(buf, bufsize, " %s", NO_VAL_STR);

	if (util_run_cmd(cmdbuf, sizeof(cmdbuf), argv) != 0) {
		return (comp_ret_t){ false, "Error running notmuch" };
	}
	errno = 0; /* To distinguish success/failure after call */
	count = strtol(cmdbuf, NULL, 0);
	assert(!errno);

	snprintf(buf, bufsize, "%s %ld", (count ? "" : ""), count);

	return (comp_ret_t){ .ok = true };
}

static comp_ret_t parse_meminfo(char *out, const size_t outsize, char *data,
				const size_t insize)
{
	data[insize - 1] = '\0';

	char *s = strstr(data, "MemAvailable");
	if (s == NULL) {
		return (comp_ret_t){ false, "Unable to parse meminfo" };
	}
	int i;
	char *p, *token, *saveptr;
	for (i = 0, p = s; i < 2; i++, p = NULL) {
		token = strtok_r(p, " ", &saveptr);
		if (!token) {
			return (comp_ret_t){ false, "Unable to parse meminfo" };
		}
	}
	uintmax_t val = strtoumax(token, NULL, 0);
	assert(val != 0 && val != INTMAX_MAX && val != UINTMAX_MAX);

	util_fmt_human(out, outsize, val * K_IEC, K_IEC);
	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_cpu(char *buf, const size_t bufsize, const char *args)
{
	const char proc_stat[] = "/proc/stat";

	snprintf(buf, bufsize, " %s", NO_VAL_STR);

	FILE *fp = fopen(proc_stat, "r");
	if (!fp) {
		return (comp_ret_t){ false, "Error opening /proc/stat" };
	}

	uint64_t t[7];
	int n = fscanf(fp, "cpu  %lu %lu %lu %lu %lu %lu %lu", &t[0], &t[1],
		       &t[2], &t[3], &t[4], &t[5], &t[6]);
	fclose(fp);
	if (n != LEN(t)) {
		return (comp_ret_t){ false, "Error reading /proc/stat" };
	}

	static uint64_t total_prev, idle_prev;

	uint64_t total_cur = t[0] + t[1] + t[2] + t[3] + t[4] + t[5] + t[6];
	uint64_t idle_cur = t[3];

	pthread_mutex_lock(&cpu_data_mtx);
	uint64_t total = total_cur - total_prev;
	uint64_t idle = idle_cur - idle_prev;
	total_prev = total_cur;
	idle_prev = idle_cur;
	pthread_mutex_unlock(&cpu_data_mtx);

	uint64_t usage = 100 * (total - idle) / total;
	snprintf(buf, bufsize, " %ld%%", usage);

	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_mem_avail(char *buf, const size_t bufsize,
				 const char *args)
{
	snprintf(buf, bufsize, " %s", NO_VAL_STR);

	FILE *f = fopen("/proc/meminfo", "r");
	if (!f) {
		return (comp_ret_t){ false, "Error opening /proc/meminfo" };
	}
	char *data = NULL;
	size_t len;
	ssize_t nread = getdelim(&data, &len, '\0', f);
	if (nread == -1) {
		free(data);
		fclose(f);
		return (comp_ret_t){ false, "Error reading /proc/meminfo" };
	}
	char value[MSG_LEN];
	comp_ret_t ret = parse_meminfo(value, bufsize, data, nread);
	free(data);
	fclose(f);
	if (!ret.ok) {
		return ret;
	}

	snprintf(buf, bufsize, " %s", value);

	return (comp_ret_t){ .ok = true };
}

static comp_ret_t wifi_essid(char *buf, const char *iface)
{
	assert(iface);
	size_t len = strlen(iface);
	assert(len < IFNAMSIZ);

	struct iwreq iwreq = { 0 };
	iwreq.u.essid.length = IW_ESSID_MAX_SIZE + 1;
	memcpy(iwreq.ifr_name, iface, len);

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		return (comp_ret_t){ false, "Unable to create socket" };
	}
	iwreq.u.essid.pointer = buf;
	if (ioctl(sockfd, SIOCGIWESSID, &iwreq) < 0) {
		close(sockfd);
		return (comp_ret_t){ false, "Unable to get ESSID" };
	}

	close(sockfd);
	return (comp_ret_t){ .ok = true };
}

static comp_ret_t parse_wireless(char *out, const size_t outsize,
				 const char *iface, char *data,
				 const size_t insize)
{
	data[insize - 1] = '\0';

	char *s = strstr(data, iface);
	if (s == NULL) {
		return (comp_ret_t){ false, "Unable to parse wifi strength" };
	}
	int i;
	char *p, *token, *saveptr;
	for (i = 0, p = s; i < 3; i++, p = NULL) {
		token = strtok_r(p, " ", &saveptr);
		if (!token) {
			return (comp_ret_t){ false,
					     "Unable to parse wifi strength" };
		}
	}
	uintmax_t val = strtoumax(token, NULL, 0);
	assert(val != 0 && val != INTMAX_MAX && val != UINTMAX_MAX);

	/* 70 is the max of /proc/net/wireless */
	snprintf(out, outsize, "%ld", (val * 100 / 70));
	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_wifi(char *buf, const size_t bufsize, const char *device)
{
	char essid[IW_ESSID_MAX_SIZE + 1];
	wifi_essid(essid, device);

	snprintf(buf, bufsize, "󰖪 %s", NO_VAL_STR);

	FILE *f = fopen("/proc/net/wireless", "r");
	if (!f) {
		return (comp_ret_t){ false,
				     "Error opening /proc/net/wireless" };
	}
	char *data = NULL;
	size_t len;
	ssize_t nread = getdelim(&data, &len, '\0', f);
	if (nread == -1) {
		free(data);
		fclose(f);
		return (comp_ret_t){ false,
				     "Error reading /proc/net/wireless" };
	}
	char value[MSG_LEN];
	comp_ret_t ret =
		parse_wireless(value, sizeof(value), device, data, nread);
	free(data);
	fclose(f);
	if (!ret.ok) {
		return ret;
	}

	snprintf(buf, bufsize, " %s %s%%", essid, value);

	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_disk_free(char *buf, const size_t bufsize,
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

static comp_ret_t comp_volume(char *buf, const size_t bufsize, const char *path)
{
	char cmdbuf[MSG_LEN] = { 0 };
	char *const argv[] = { "pamixer", "--get-volume-human", NULL };

	snprintf(buf, bufsize, "󰝟 %s", NO_VAL_STR);

	if (util_run_cmd(cmdbuf, sizeof(cmdbuf), argv) != 0) {
		return (comp_ret_t){ false, "Error running pamixer" };
	}

	snprintf(buf, bufsize, "󰕾 %s", cmdbuf);
	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_datetime(char *buf, const size_t bufsize,
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
