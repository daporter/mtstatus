#include "util.h"

#include <string.h>

#include "errors.h"

static char *util_cat(char *dest, const char *end, const char *str)
{
	while (dest < end && *str)
		*dest++ = *str++;
	return dest;
}

size_t util_join_strings(char *buf, size_t bufsize, const char *delim,
			 const size_t nstr, const size_t slen,
			 char strings[nstr][slen])
{
	size_t i = 0;
	char *ptr = buf;
	char *end = buf + bufsize;

	for (i = 0; (ptr < end) && (i < nstr - 1); i++)
		if (strlen(strings[i]) > 0) {
			ptr = util_cat(ptr, end, strings[i]);
			ptr = util_cat(ptr, end, delim);
		}
	if ((ptr < end) && (strlen(strings[i]) > 0))
		ptr = util_cat(ptr, end, strings[i]);

	return ptr - buf;
}

int util_fmt_human(char *buf, int len, uintmax_t num, int base)
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
		app_warn("[fmt_human] Invalid base");
		return -1;
	}

	scaled = num;
	for (i = 0; i < prefixlen && scaled >= base; i++)
		scaled /= base;

	return Snprintf(buf, len, "%.1f %s", scaled, prefix[i]);
}
