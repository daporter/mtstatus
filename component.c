#include "mtstatus.h"
#include "util.h"

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

#define BUF_SIZE 128

typedef struct comp_ret comp_ret_t;
struct comp_ret {
	bool ok;
	char message[BUF_SIZE];
};

pthread_mutex_t cpu_data_mtx = PTHREAD_MUTEX_INITIALIZER;

static comp_ret_t comp_keyb_ind(char *buf, const size_t bufsize,
				const char *args)
{
	XKeyboardState state;
	XGetKeyboardControl(dpy, &state);

	bool caps_on    = state.led_mask & (1 << 0);
	bool numlock_on = state.led_mask & (1 << 1);
	char *val = "";
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
	char *const argv[]= { "notmuch",
			       "count",
			       "tag:unread NOT tag:archived",
			       NULL };
	char cmdbuf[BUF_SIZE] = { 0 };
	if (util_run_cmd(cmdbuf, sizeof(cmdbuf), argv) != 0) {
		snprintf(buf, bufsize, " %s", NO_VAL_STR);
		return (comp_ret_t){ false, "Error running notmuch" };
	}
	errno = 0;		/* To distinguish success/failure after call */
	long count = strtol(cmdbuf, NULL, 0);
	assert(!errno);

	snprintf(buf, bufsize, "%s %ld", (count ? "" : ""), count);

	return (comp_ret_t){ .ok = true };
}

static comp_ret_t parse_meminfo(char *out, const size_t outsize, char *data,
				const size_t datasz)
{
	data[datasz - 1] = '\0';

	char *s = strstr(data, "MemAvailable");
	if (s == NULL) {
		return (comp_ret_t){ false, "Unable to parse meminfo" };
	}
	int i;
	char *p, *token, *saveptr;
	for (i = 0, p = s;
	     i < 2;
	     i++, p = NULL) {
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
	FILE *fp = fopen("/proc/stat", "r");
	if (!fp) {
		snprintf(buf, bufsize, " %s", NO_VAL_STR);
		return (comp_ret_t){ false, "Error opening /proc/stat" };
	}

	uint64_t t[7];
	int n = fscanf(fp,
		       "cpu  %lu %lu %lu %lu %lu %lu %lu",
		       &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6]);
	fclose(fp);
	if (n != LEN(t)) {
		snprintf(buf, bufsize, " %s", NO_VAL_STR);
		return (comp_ret_t){ false, "Error reading /proc/stat" };
	}

	static uint64_t total_prev, idle_prev;

	uint64_t total_cur	= t[0] + t[1] + t[2] + t[3] + t[4] + t[5] + t[6];
	uint64_t idle_cur	= t[3];

	pthread_mutex_lock(&cpu_data_mtx);
	uint64_t total	= total_cur - total_prev;
	uint64_t idle	= idle_cur - idle_prev;
	total_prev	= total_cur;
	idle_prev	= idle_cur;
	pthread_mutex_unlock(&cpu_data_mtx);

	uint64_t usage = 100 * (total - idle) / total;
	snprintf(buf, bufsize, " %ld%%", usage);

	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_mem_avail(char *buf, const size_t bufsize,
				 const char *args)
{
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f) {
		snprintf(buf, bufsize, " %s", NO_VAL_STR);
		return (comp_ret_t){ false, "Error opening /proc/meminfo" };
	}
	char	*data = NULL;
	size_t	 len;
	ssize_t	 nread = getdelim(&data, &len, '\0', f);
	if (nread == -1) {
		free(data);
		fclose(f);
		snprintf(buf, bufsize, " %s", NO_VAL_STR);
		return (comp_ret_t){ false, "Error reading /proc/meminfo" };
	}
	char value[BUF_SIZE];
	comp_ret_t ret = parse_meminfo(value, bufsize, data, nread);
	free(data);
	fclose(f);
	if (!ret.ok) {
		snprintf(buf, bufsize, " %s", NO_VAL_STR);
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
				 const size_t datasz)
{
	data[datasz - 1] = '\0';

	char *s = strstr(data, iface);
	if (s == NULL) {
		return (comp_ret_t){ false, "Unable to parse wifi strength" };
	}
	int i;
	char *p, *token, *saveptr;
	for (i = 0, p = s;
	     i < 3;
	     i++, p = NULL) {
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

	FILE *f = fopen("/proc/net/wireless", "r");
	if (!f) {
		snprintf(buf, bufsize, "󰖪 %s", NO_VAL_STR);
		return (comp_ret_t){ false,
				     "Error opening /proc/net/wireless" };
	}
	char *data = NULL;
	size_t len;
	ssize_t nread = getdelim(&data, &len, '\0', f);
	if (nread == -1) {
		free(data);
		fclose(f);
		snprintf(buf, bufsize, "󰖪 %s", NO_VAL_STR);
		return (comp_ret_t){ false,
				     "Error reading /proc/net/wireless" };
	}
	char value[BUF_SIZE];
	comp_ret_t ret;
	ret = parse_wireless(value, sizeof(value), device, data, nread);
	free(data);
	fclose(f);
	if (!ret.ok) {
		return ret;
	}
	snprintf(buf, bufsize, " %s%% %s", value, essid);
	return (comp_ret_t){ .ok = true };
}

static comp_ret_t ret_err(char *buf, const size_t bufsize, const char* icon,
			  const char *fmt, const int errnum)
{
	snprintf(buf, bufsize, "%s %s", icon, NO_VAL_STR);

	comp_ret_t ret;
	ret.ok = false;
	char errbuf[BUF_SIZE];
	strerror_r(errnum, errbuf, sizeof(errbuf));
	snprintf(ret.message, sizeof(ret.message), fmt, errbuf);
	return ret;
}

static comp_ret_t comp_disk_free(char *buf, const size_t bufsize,
				 const char *path)
{
	comp_ret_t ret;
	struct statvfs fs;
	int r = statvfs(path, &fs);
	if (r < 0) {
		return ret_err(buf, bufsize, "󰋊", "statvfs: '%s': %s", r);
	}

	char output[bufsize];
	util_fmt_human(output, sizeof(output), fs.f_frsize * fs.f_bavail,
		       K_IEC);
	snprintf(buf, bufsize, "󰋊 %s", output);
	ret.ok = true;
	return ret;
}

static comp_ret_t comp_volume(char *buf, const size_t bufsize, const char *path)
{
	char *const argv[] = { "pamixer", "--get-volume-human", NULL };
	char cmdbuf[BUF_SIZE] = { 0 };
	if (util_run_cmd(cmdbuf, sizeof(cmdbuf), argv) != 0) {
		snprintf(buf, bufsize, "%s %s", "󰝟", NO_VAL_STR);
		return (comp_ret_t){ false, "Error running pamixer" };
	}
	snprintf(buf, bufsize, "%s %s", "󰕾", cmdbuf);
	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_datetime(char *buf, const size_t bufsize,
				const char *date_fmt)
{
	char icon[] = "";
	comp_ret_t ret;
	time_t t = time(NULL);
	if (t == -1) {
		return ret_err(buf, bufsize, icon, "time: %s", errno);
	}
	struct tm now;
	if (localtime_r(&t, &now) == NULL) {
		return ret_err(buf, bufsize,
			       icon, "Unable to determine local time: %s",
			       errno);
	}
	char output[bufsize];
	if (strftime(output, sizeof(output), date_fmt, &now) == 0) {
		snprintf(buf, bufsize, "%s %s", icon, NO_VAL_STR);
		return (comp_ret_t){ false, "Unable to format time" };
	}
	snprintf(buf, bufsize, "%s %s", icon, output);
	ret.ok = true;
	return ret;
}
