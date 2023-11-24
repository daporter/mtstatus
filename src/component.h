#ifndef COMPONENT_H
#define COMPONENT_H

#include <stdbool.h>
#include <stddef.h>

#define MAXLEN 128

typedef struct {
	bool ok;
	char message[MAXLEN];
} comp_ret_t;

comp_ret_t component_keyb_ind(char *buf, const size_t bufsize, const char *args,
			      const char *no_val_str);
comp_ret_t component_notmuch(char *buf, const size_t bufsize, const char *args,
			     const char *no_val_str);
comp_ret_t component_parse_meminfo(char *out, const size_t outsize, char *in,
				   const size_t insize);
comp_ret_t component_mem_avail(char *buf, const size_t bufsize,
			       const char *args, const char *no_val_str);
comp_ret_t component_disk_free(char *buf, const size_t bufsize,
			       const char *path, const char *no_val_str);
comp_ret_t component_datetime(char *buf, const size_t bufsize,
			      const char *date_fmt, const char *no_val_str);
#endif
