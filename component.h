#ifndef COMPONENTS_H
#define COMPONENTS_H

void keyb_ind(char *buf, int bufsize, const char *args, const char *no_val_str);
void notmuch(char *buf, int bufsize, const char *args, const char *no_val_str);
void load_avg(char *buf, int bufsize, const char *args, const char *no_val_str);
void ram_free(char *buf, int bufsize, const char *args, const char *no_val_str);
void disk_free(char *buf, int bufsize, const char *path, const char *no_val_str);
void datetime(char *buf, int bufsize, const char *date_fmt, const char *no_val_str);

#endif
