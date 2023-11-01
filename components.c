#include "components.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/statvfs.h>

#include "config.h"
#include "errors.h"
#include "util.h"

void datetime(char *buf)
{
	time_t t;
	struct tm now;

	if ((t = time(NULL)) == -1) {
		unix_warn("[datetime] Unable to obtain the current time");
		Snprintf(buf, MAX_COMP_LEN, "%s", no_val_str);
		return;
	}
	if (localtime_r(&t, &now) == NULL) {
		unix_warn("[datetime] Unable to determine local time");
		Snprintf(buf, MAX_COMP_LEN, "%s", no_val_str);
		return;
	}
	if (strftime(buf, MAX_COMP_LEN, "  %a %d %b %R", &now) == 0)
		unix_warn("[datetime] Unable to format time");
}

void ram_free(char *buf)
{
	assert(buf != NULL);

	const char meminfo[] = "/proc/meminfo";
	FILE *fp;
	char total_str[MAX_COMP_LEN], free_str[MAX_COMP_LEN];
	uintmax_t free;

	if ((fp = fopen(meminfo, "r")) == NULL) {
		unix_warn("[ram_free] Unable to open %s", meminfo);
		Snprintf(buf, MAX_COMP_LEN, "%s", no_val_str);
		return;
	}
	if (fscanf(fp,
		   "MemTotal: %s kB\n"
		   "MemFree: %s kB\n",
		   total_str, free_str) == EOF) {
		unix_warn("[ram_free] Unable to parse %s", meminfo);
		Snprintf(buf, MAX_COMP_LEN, "%s", no_val_str);
		Fclose(fp);
		return;
	}
	Fclose(fp);

	if ((free = strtoumax(free_str, NULL, 0)) == 0 || free == INTMAX_MAX ||
	    free == UINTMAX_MAX) {
		app_warn("[ram_free] Unable to convert value %s", free_str);
		Snprintf(buf, MAX_COMP_LEN, "%s", no_val_str);
		return;
	}

	fmt_human(free_str, LEN(free_str), free * K_IEC, K_IEC);
	Snprintf(buf, MAX_COMP_LEN, "  %s", free_str);
}

void disk_free(char *buf)
{
	const char path[] = "/";
	struct statvfs fs;
	char free_str[MAX_COMP_LEN];

	if (statvfs(path, &fs) < 0) {
		unix_warn("[disk_free] statvfs '%s':", path);
		Snprintf(buf, MAX_COMP_LEN, "%s", no_val_str);
		return;
	}

	fmt_human(free_str, MAX_COMP_LEN, fs.f_frsize * fs.f_bavail, K_IEC);
	Snprintf(buf, MAX_COMP_LEN, "󰋊 %s", free_str);
}

void load_avg(char *buf)
{
	double avgs[1];

	if (getloadavg(avgs, 1) < 0) {
		unix_warn("[load_avg] Failed to obtain load average");
		Snprintf(buf, MAX_COMP_LEN, "%s", no_val_str);
		return;
	}

	Snprintf(buf, MAX_COMP_LEN, "  %.2f", avgs[0]);
}
