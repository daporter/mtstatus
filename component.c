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
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define BUF_SIZE 128

typedef bool (*parser_t)(char *, const size_t, char *, const size_t,
			 const char *);

pthread_mutex_t cpu_data_mtx    = PTHREAD_MUTEX_INITIALIZER,
		net_traffic_mtx = PTHREAD_MUTEX_INITIALIZER;

static void render_component(char *buf, const size_t bufsize,
			     const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, bufsize, fmt, ap);
	va_end(ap);
	assert(n >= 0 && (size_t)n < bufsize);
}

void comp_keyb_ind(char *buf, const size_t bufsize, const char *args)
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
}

void comp_notmuch(char *buf, const size_t bufsize, const char *args)
{
	char *const argv[]= { "notmuch",
			       "count",
			       "tag:unread NOT tag:archived",
			       NULL };
	char cmdbuf[BUF_SIZE] = { 0 };
	bool s = util_run_cmd(cmdbuf, sizeof(cmdbuf), argv);
	if (!s) {
		log_err("Unable to run 'notmuch'");
		render_component(buf, bufsize, " %s", ERR_STR);
		return;
	}
	errno = 0;
	long count = strtol(cmdbuf, NULL, 0);
	assert(!errno);
	render_component(buf, bufsize, "%s %ld", (count ? "" : ""), count);
}

bool parse_net_stats(char *buf, const size_t bufsize, uint64_t *val, char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		log_errno(errno, "Error: unable to open '%s'", path);
		return false;
	}
	int n = fscanf(f, "%lu", val);
	(void)fclose(f);
	if (n != 1) {
		log_err("Error: unable to parse '%s'", path);
		return false;
	}
	return true;
}

void comp_net_traffic(char *buf, const size_t bufsize, const char *iface)
{
	char path[PATH_MAX];

	// TODO(david): Remove the duplication here.

	int n = snprintf(path, sizeof(path),
		 "/sys/class/net/%s/statistics/rx_bytes", iface);
	if (n < 0) {
		log_err("Error creating rx_bytes filepath for '%s'", iface);
		render_component(buf, bufsize, "%s▾ %s▴", ERR_STR, ERR_STR);
		return;
	}
	if ((size_t)n >= sizeof(path)) {
		log_err("Error: rx_bytes filepath for '%s' too big", iface);
		render_component(buf, bufsize, "%s▾ %s▴", ERR_STR, ERR_STR);
		return;
	}

	uint64_t rx_cur, tx_cur;

	bool s = parse_net_stats(buf, bufsize, &rx_cur, path);
	if (!s) {
		log_err("Unable to parse network rx bytes");
		render_component(buf, bufsize, "%s▾ %s▴", ERR_STR, ERR_STR);
		return;
	}

	n = snprintf(path, sizeof(path),
		     "/sys/class/net/%s/statistics/tx_bytes", iface);
	if (n < 0) {
		log_err("Error creating tx_bytes filepath for '%s'", iface);
		render_component(buf, bufsize, "%s▾ %s▴", ERR_STR, ERR_STR);
		return;
	}
	if ((size_t)n >= sizeof(path)) {
		log_err("Error: tx_bytes filepath for '%s' too big", iface);
		render_component(buf, bufsize, "%s▾ %s▴", ERR_STR, ERR_STR);
		return;
	}

	s = parse_net_stats(buf, bufsize, &tx_cur, path);
	if (!s) {
		log_err("Unable to parse network tx bytes");
		render_component(buf, bufsize, "%s▾ %s▴", ERR_STR, ERR_STR);
		return;
	}

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
	render_component(buf, bufsize, "%7s%s▾ %7s%s▴",
			 rx_buf, "B", tx_buf, "B");
}

void comp_cpu(char *buf, const size_t bufsize, const char *args)
{
	char file[] = "/proc/stat";
	FILE *fp = fopen(file, "r");
	if (!fp) {
		log_errno(errno, "Error: unable to open '%s'", file);
		render_component(buf, bufsize, " %s", ERR_STR);
		return;
	}

	uint64_t t[7];
	int n = fscanf(fp,
		       "cpu  %lu %lu %lu %lu %lu %lu %lu",
		       &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6]);
	(void)fclose(fp);
	if (n != LEN(t)) {
		log_err("Error parsing '%s'", file);
		render_component(buf, bufsize, " %s", ERR_STR);
		return;
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
	render_component(buf, bufsize, " %ld%%", usage);
}

bool parse_val(char *data, size_t datasz,
	       const char *target, unsigned nfield,
	       long *result)
{
	data[datasz - 1] = '\0';

	char *s = strstr(data, target);
	if (!s) return false;
	unsigned i;
	char *token = NULL;
	char *p, *saveptr;
	for (i = 0, p = s; i < nfield; i++, p = NULL) {
		token = strtok_r(p, " ", &saveptr);
		if (!token) return false;
	}
	assert(token);
	errno = 0;
	*result = strtol(token, NULL, 0);
	if (errno) {
		log_errno(errno, "Error converting '%s'", token);
		return false;
	}
	return true;
}

bool parse_meminfo(char *out, const size_t outsize,
		   char *data, const size_t datasz,
		   const char *target)
{
	long val;
	bool s = parse_val(data, datasz, target, 2, &val);
	if (!s) {
		log_err("Unable to parse available memory");
		return false;
	}
	util_fmt_human(out, outsize, val * K_IEC, K_IEC);
	return true;
}

bool parse_wireless(char *out, const size_t outsize,
		    char *data, const size_t datasz,
		    const char *target)
{
	long val;
	bool s = parse_val(data, datasz, target, 3, &val);
	if (!s) {
		log_err("Unable to parse wifi strength");
		return false;
	}

	/* 70 is the max of /proc/net/wireless */
	render_component(out, outsize, "%ld", (val * 100 / 70));
	return true;
}

bool parse_file(char *buf, const size_t bufsize,
		char *file, const char *target, parser_t parse)
{
	bool ret = false;

	FILE *f = fopen(file, "r");
	if (!f) {
		log_errno(errno, "Error: unable to open '%s'", file);
		goto out;
	}
	char *data = NULL;
	size_t len;
	errno = 0;
	ssize_t nread = getdelim(&data, &len, '\0', f);
	if (nread == -1) {
		log_errno(errno, "Error reading '%s'", file);
		goto cleanup;
	}
	bool s = parse(buf, bufsize, data, nread, target);
	if (!s) {
		goto cleanup;
	}
	ret = true;

cleanup:
	free(data);
	(void)fclose(f);
out:
	return ret;
}

void comp_mem_avail(char *buf, const size_t bufsize, const char *args)
{
	char value[BUF_SIZE];
	bool s = parse_file(value, sizeof(value),
			    "/proc/meminfo", "MemAvailable", parse_meminfo);
	if (!s) {
		log_err("Unable to determine available memory");
		render_component(buf, bufsize, " %s", ERR_STR);
		return;
	}
	render_component(buf, bufsize, " %s%s", value, "B");
}

bool wifi_essid(char *buf, const char *iface)
{
	bool ret = false;

	assert(iface);
	size_t len = strlen(iface);
	assert(len < IFNAMSIZ);

	struct iwreq iwreq = { 0 };
	iwreq.u.essid.length = IW_ESSID_MAX_SIZE + 1;
	memcpy(iwreq.ifr_name, iface, len);

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd == -1) {
		log_errno(errno, "Error creating socket: %s");
		goto out;
	}
	iwreq.u.essid.pointer = buf;
	if (ioctl(sockfd, SIOCGIWESSID, &iwreq) == -1) {
		log_errno(errno, "Error reading socket: %s");
		goto cleanup;
	}
	ret = true;

cleanup:
	close(sockfd);
out:
	return ret;
}

void comp_wifi(char *buf, const size_t bufsize, const char *device)
{
	char essid[IW_ESSID_MAX_SIZE + 1];
	wifi_essid(essid, device);

	char value[BUF_SIZE];
	bool s = parse_file(value, sizeof(value),
			    "/proc/net/wireless", device, parse_wireless);
	if (!s) {
		log_err("Unable to determine wifi strength");
		render_component(buf, bufsize, " %s", ERR_STR);
		return;
	}
	render_component(buf, bufsize, " %s%% %s", value, essid);
}

void comp_disk_free(char *buf, const size_t bufsize, const char *path)
{
	struct statvfs fs;
	int r = statvfs(path, &fs);
	if (r == -1) {
		log_errno(errno, "Error: statvfs: %s");
		log_err("Unable to determine disk free space");
		render_component(buf, bufsize, "󰋊 %s", ERR_STR);
		return;
	}
	char output[bufsize];
	util_fmt_human(output, sizeof(output),
		       fs.f_frsize * fs.f_bavail, K_IEC);
	render_component(buf, bufsize, "󰋊 %sB", output);
}

void comp_volume(char *buf, const size_t bufsize, const char *path)
{
	char *const argv[] = { "pamixer", "--get-volume-human", NULL };

	char cmdbuf[BUF_SIZE] = { 0 };
	bool s = util_run_cmd(cmdbuf, sizeof(cmdbuf), argv);
	if (!s) {
		log_err("Unable to determine volume");
		render_component(buf, bufsize, "󰝟 %s", ERR_STR);
		return;
	}
	render_component(buf, bufsize, "󰕾 %s", cmdbuf);
}

void comp_datetime(char *buf, const size_t bufsize, const char *date_fmt)
{
	time_t t = time(NULL);
	assert(t != -1);
	struct tm now;
	struct tm *ret_l = localtime_r(&t, &now);
	assert(ret_l);
	char output[bufsize];
	size_t ret_s = strftime(output, sizeof(output), date_fmt, &now);
	assert(ret_s);
	render_component(buf, bufsize, "  %s", output);
}
