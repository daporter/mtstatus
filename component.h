#ifndef COMPONENT_H
#define COMPONENT_H

#include <sys/types.h>

void comp_keyb_ind(char *buf, size_t bufsize, const char *args);
void comp_notmuch(char *buf, size_t bufsize, const char *args);
void comp_net_traffic(char *buf, size_t bufsize, const char *iface);
void comp_cpu(char *buf, size_t bufsize, const char *args);
void comp_mem_avail(char *buf, size_t bufsize, const char *args);
void comp_disk_free(char *buf, size_t bufsize, const char *path);
void comp_volume(char *buf, size_t bufsize, const char *path);
void comp_wifi(char *buf, size_t bufsize, const char *device);
void comp_datetime(char *buf, size_t bufsize, const char *date_fmt);

#endif
