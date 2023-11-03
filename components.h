#ifndef COMPONENTS_H
#define COMPONENTS_H

void keyboard_indicators(char *buf, int bufsize, const char *args);
void notmuch(char *buf, int bufsize, const char *args);
void load_avg(char *buf, int bufsize, const char *args);
void ram_free(char *buf, int bufsize, const char *args);
void disk_free(char *buf, int bufsize, const char *path);
void datetime(char *buf, int bufsize, const char *date_fmt);

#endif
