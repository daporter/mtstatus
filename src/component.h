#ifndef COMPONENT_H
#define COMPONENT_H

#define DIVIDER	   "  "
#define NO_VAL_STR "n/a"

#include <stdbool.h>
#include <stddef.h>

#define MAXLEN 128

typedef struct {
	bool ok;
	char message[MAXLEN];
} comp_ret_t;

comp_ret_t component_keyb_ind(char *buf, size_t bufsize, const char *args);
comp_ret_t component_notmuch(char *buf, size_t bufsize, const char *args);
comp_ret_t component_parse_meminfo(char *out, size_t outsize, char *in,
				   size_t insize);
comp_ret_t component_mem_avail(char *buf, size_t bufsize, const char *args);
comp_ret_t component_disk_free(char *buf, size_t bufsize, const char *path);
comp_ret_t component_datetime(char *buf, size_t bufsize, const char *date_fmt);

#endif
