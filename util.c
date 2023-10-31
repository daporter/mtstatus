#include "util.h"

#include "errors.h"

int fmt_human(char *buf, int len, uintmax_t num, int base)
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
