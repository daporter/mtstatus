#ifndef COMPONENTS_H
#define COMPONENTS_H

void load_avg(char *buf, const int bufsize, const char *args);
void ram_free(char *buf, const int bufsize, const char *args);
void disk_free(char *buf, const int bufsize, const char *path);
void datetime(char *buf, const int bufsize, const char *date_fmt);

#endif
