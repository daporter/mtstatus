#include "components.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

#include "config.h"
#include "errors.h"
#include "util.h"

static bool run_cmd(char *buf, const char *cmd)
{
	char *p;
	FILE *fp;

	if (!(fp = popen(cmd, "r"))) { /* NOLINT(cert-env33-c)*/
		unix_warn("popen '%s':", cmd);
		return false;
	}

	p = fgets(buf, sizeof(buf) - 1, fp);
	if (pclose(fp) < 0) {
		unix_warn("pclose '%s':", cmd);
		return false;
	}
	if (!p)
		return false;

	if ((p = strrchr(buf, '\n')))
		p[0] = '\0';

	return true;
}

void notmuch(char *buf, const int bufsize, const char *unused(args))
{
	char output[bufsize];
	long count;

	if (!run_cmd(output, "notmuch count 'tag:unread NOT tag:archived'")) {
		unix_warn("[notmuch] Failed to run notmuch command");
		Snprintf(buf, bufsize, "  %s", no_val_str);
		return;
	}

	errno = 0; /* To distinguish success/failure after call */
	count = strtol(output, NULL, 0);
	if (errno != 0) {
		unix_warn("[notmuch] Failed to convert command output");
		Snprintf(buf, bufsize, "  %s", no_val_str);
		return;
	}
	if (count == 0)
		Snprintf(buf, bufsize, "  %ld", count);
	else
		Snprintf(buf, bufsize, "  %ld", count);
}

void load_avg(char *buf, const int bufsize, const char *unused(args))
{
	double avgs[1];
	char output[bufsize];

	if (getloadavg(avgs, 1) < 0) {
		unix_warn("[load_avg] Failed to obtain load average");
		Snprintf(buf, bufsize, "  %s", no_val_str);
		return;
	}

	Snprintf(output, LEN(output), "%.2f", avgs[0]);
	Snprintf(buf, bufsize, "  %s", output);
}

void ram_free(char *buf, const int bufsize, const char *unused(args))
{
	const char meminfo[] = "/proc/meminfo";
	FILE *fp;
	char total_str[bufsize], free_str[bufsize];
	uintmax_t free;

	assert(buf != NULL);

	if ((fp = fopen(meminfo, "r")) == NULL) {
		unix_warn("[ram_free] Unable to open %s", meminfo);
		Snprintf(buf, bufsize, "  %s", no_val_str);
		return;
	}
	if (fscanf(fp,
		   "MemTotal: %s kB\n"
		   "MemFree: %s kB\n",
		   total_str, free_str) == EOF) {
		unix_warn("[ram_free] Unable to parse %s", meminfo);
		Snprintf(buf, bufsize, "  %s", no_val_str);
		Fclose(fp);
		return;
	}
	Fclose(fp);

	if ((free = strtoumax(free_str, NULL, 0)) == 0 || free == INTMAX_MAX ||
	    free == UINTMAX_MAX) {
		app_warn("[ram_free] Unable to convert value %s", free_str);
		Snprintf(buf, bufsize, "  %s", no_val_str);
		return;
	}

	fmt_human(free_str, LEN(free_str), free * K_IEC, K_IEC);
	Snprintf(buf, bufsize, "  %s", free_str);
}

void disk_free(char *buf, const int bufsize, const char *path)
{
	struct statvfs fs;
	char output[bufsize];

	if (statvfs(path, &fs) < 0) {
		unix_warn("[disk_free] statvfs '%s':", path);
		Snprintf(buf, bufsize, "󰋊 %s", no_val_str);
		return;
	}

	fmt_human(output, LEN(output), fs.f_frsize * fs.f_bavail, K_IEC);
	Snprintf(buf, bufsize, "󰋊 %s", output);
}

void datetime(char *buf, const int bufsize, const char *date_fmt)
{
	time_t t;
	struct tm now;
	char output[bufsize];

	if ((t = time(NULL)) == -1) {
		unix_warn("[datetime] Unable to obtain the current time");
		Snprintf(buf, bufsize, "  %s", no_val_str);
		return;
	}
	if (localtime_r(&t, &now) == NULL) {
		unix_warn("[datetime] Unable to determine local time");
		Snprintf(buf, bufsize, "  %s", no_val_str);
		return;
	}
	if (strftime(output, LEN(output), date_fmt, &now) == 0)
		unix_warn("[datetime] Unable to format time");
	Snprintf(buf, bufsize, "  %s", output);
}
