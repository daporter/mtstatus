#include "mtstatus.h"
#include "util.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/if.h>
#include <linux/limits.h>
#include <linux/wireless.h>
#include <pthread.h>
#include <signal.h>
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

pthread_mutex_t cpu_data_mtx    = PTHREAD_MUTEX_INITIALIZER,
		net_traffic_mtx = PTHREAD_MUTEX_INITIALIZER;

static comp_ret_t comp_keyb_ind(char *buf, const size_t bufsize,
				const char *args)
{
	if (dpy) {
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
	}
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

static comp_ret_t ret_err(char *buf, const size_t bufsize, const char* icon,
			  const char *fmt, const int errnum)
{
	char errbuf[BUF_SIZE];
	strerror_r(errnum, errbuf, sizeof(errbuf));

	snprintf(buf, bufsize, "%s %s", icon, NO_VAL_STR);

	comp_ret_t ret;
	ret.ok = false;
	snprintf(ret.message, sizeof(ret.message), fmt, errbuf);
	return ret;
}

comp_ret_t parse_net_stats(char *buf, const size_t bufsize,
			   uint64_t *val, char *path)
{
	comp_ret_t ret;

	FILE *f = fopen(path, "r");
	if (!f) {
		snprintf(buf, bufsize, "▾%s ▴%s", NO_VAL_STR, NO_VAL_STR);
		ret.ok = false;
		snprintf(ret.message, sizeof(ret.message), "Unable to open %s", path);
		return ret;
	}
	int n = fscanf(f, "%lu", val);
	fclose(f);
	if (n != 1) {
		snprintf(buf, bufsize, "▾%s ▴%s", NO_VAL_STR, NO_VAL_STR);
		ret.ok = false;
		snprintf(ret.message, sizeof(ret.message), "Unable to parse %s", path);
		return ret;
	}
	ret.ok = true;
	return ret;
}

static comp_ret_t comp_net_traffic(char *buf, const size_t bufsize,
				   const char *iface)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path),
		 "/sys/class/net/%s/statistics/rx_bytes", iface);
	uint64_t rx_cur, tx_cur;
	parse_net_stats(buf, bufsize, &rx_cur, path);

	snprintf(path, sizeof(path),
		 "/sys/class/net/%s/statistics/tx_bytes", iface);
	parse_net_stats(buf, bufsize, &tx_cur, path);

	static uint64_t rx_prev, tx_prev;

	pthread_mutex_lock(&net_traffic_mtx);
	uint64_t rx = rx_cur - rx_prev;
	uint64_t tx = tx_cur - tx_prev;
	rx_prev = rx_cur;
	tx_prev = tx_cur;
	pthread_mutex_unlock(&net_traffic_mtx);

	char rx_buf[BUF_SIZE], tx_buf[BUF_SIZE];
	util_fmt_human(rx_buf, sizeof(rx_buf), rx, K_IEC);
	util_fmt_human(tx_buf, sizeof(tx_buf), tx, K_IEC);
	snprintf(buf, bufsize, "▾%s%s ▴%s%s", rx_buf, "B", tx_buf, "B");

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

	uint64_t total_cur = t[0] + t[1] + t[2] + t[3] + t[4] + t[5] + t[6];
	uint64_t idle_cur  = t[3];

	pthread_mutex_lock(&cpu_data_mtx);
	uint64_t total = total_cur - total_prev;
	uint64_t idle  = idle_cur - idle_prev;
	total_prev = total_cur;
	idle_prev  = idle_cur;
	pthread_mutex_unlock(&cpu_data_mtx);

	uint64_t usage = 100 * (total - idle) / total;
	snprintf(buf, bufsize, " %ld%%", usage);

	return (comp_ret_t){ .ok = true };
}

long parse_val(char *data, size_t datasz, const char *target, unsigned nfield)
{
	data[datasz - 1] = '\0';

	char *s = strstr(data, target);
	if (s == NULL) return -1;
	unsigned i;
	char *token = NULL;
	char *p, *saveptr;
	for (i = 0, p = s;
	     i < nfield;
	     i++, p = NULL) {
		token = strtok_r(p, " ", &saveptr);
		if (!token) return -1;
	}
	assert(token);
	return strtol(token, NULL, 0);
}

static comp_ret_t parse_meminfo(char *out, const size_t outsize,
				char *data, const size_t datasz,
				const char *target)
{
	long val = parse_val(data, datasz, target, 2);
	if (val < 0) {
		return (comp_ret_t){ false, "Unable to parse meminfo" };
	}
	util_fmt_human(out, outsize, val * K_IEC, K_IEC);
	return (comp_ret_t){ .ok = true };
}

comp_ret_t parse_file(char *buf, const size_t bufsize,
		      char *file, const char *target,
		      comp_ret_t (*parse)(char *, const size_t,
					  char *, const size_t,
					  const char *))
{
	FILE *f = fopen(file, "r");
	if (!f) {
		return (comp_ret_t){ false, "Error opening /proc/meminfo" };
	}
	char	*data = NULL;
	size_t	 len;
	ssize_t	 nread = getdelim(&data, &len, '\0', f);
	if (nread == -1) {
		free(data);
		fclose(f);
		return (comp_ret_t){ false, "Error reading /proc/meminfo" };
	}
	comp_ret_t ret = parse(buf, bufsize, data, nread, target);
	free(data);
	fclose(f);
	if (!ret.ok) {
		return ret;
	}
	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_mem_avail(char *buf, const size_t bufsize,
				 const char *args)
{
	char value[BUF_SIZE];
	comp_ret_t ret =
		parse_file(value, sizeof(value),
			   "/proc/meminfo", "MemAvailable", parse_meminfo);
	if (!ret.ok) {
		snprintf(buf, bufsize, " %s", NO_VAL_STR);
		return ret;
	}
	snprintf(buf, bufsize, " %s%s", value, "B");
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
				 char *data, const size_t datasz,
				 const char *target)
{
	long val = parse_val(data, datasz, target, 3);
	if (val < 0) {
		return (comp_ret_t){ false, "Unable to parse wifi strength" };
	}

	/* 70 is the max of /proc/net/wireless */
	snprintf(out, outsize, "%ld", (val * 100 / 70));
	return (comp_ret_t){ .ok = true };
}

static comp_ret_t comp_wifi(char *buf, const size_t bufsize, const char *device)
{
	char essid[IW_ESSID_MAX_SIZE + 1];
	wifi_essid(essid, device);

	char value[BUF_SIZE];
	comp_ret_t ret =
		parse_file(value, sizeof(value),
			   "/proc/net/wireless", device, parse_wireless);
	if (!ret.ok) {
		snprintf(buf, bufsize, " %s", NO_VAL_STR);
		return ret;
	}
	snprintf(buf, bufsize, " %s%% %s", value, essid);
	return (comp_ret_t){ .ok = true };
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
	snprintf(buf, bufsize, "󰋊 %s%s", output, "B");
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
