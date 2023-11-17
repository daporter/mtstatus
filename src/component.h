#ifndef COMPONENT_H
#define COMPONENT_H

void component_keyb_ind(char *buf, int bufsize, const char *args,
			const char *no_val_str);
void component_notmuch(char *buf, int bufsize, const char *args,
		       const char *no_val_str);
void component_load_avg(char *buf, int bufsize, const char *args,
			const char *no_val_str);
void component_ram_free(char *buf, int bufsize, const char *args,
			const char *no_val_str);
void component_disk_free(char *buf, int bufsize, const char *path,
			 const char *no_val_str);
void component_datetime(char *buf, int bufsize, const char *date_fmt,
			const char *no_val_str);

#endif
