#include "util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "errors.h"

static int evsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
	int ret;

	ret = vsnprintf(str, size, fmt, ap);
	if (ret < 0) {
		warn("vsnprintf:");
		return -1;
	}
	if ((size_t)ret >= size) {
		warn("vsnprintf: Output truncated");
		return -1;
	}

	return ret;
}

const char *bprintf(char *buf, int len, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = evsnprintf(buf, len, fmt, ap);
	va_end(ap);

	return (ret < 0) ? NULL : buf;
}

const char *fmt_human(char *buf, int len, uintmax_t num, int base)
{
	double scaled;
	size_t i, prefixlen;
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
		warn("[fmt_human] Invalid base");
		return NULL;
	}

	scaled = num;
	for (i = 0; i < prefixlen && scaled >= base; i++)
		scaled /= base;

	return bprintf(buf, len, "%.1f %s", scaled, prefix[i]);
}
