#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <stdbool.h>
#include <stdlib.h>

/* Represents a function that returns the value a status bar component
   displays */
typedef void (*updater_t)(char *buf, const int bufsize, const char *args);

/* A status bar component */
typedef struct {
	const updater_t update;
	const char *args;
	const int sleep_secs;
	const int signum;
} component_t;

typedef struct status_bar {
	component_t *components;
	size_t ncomponents;
	char *component_bufs;
	size_t max_comp_bufsize;
	const char *divider;
	bool dirty;
	pthread_mutex_t mtx;
	pthread_cond_t dirty_cnd;
} status_bar_t;

status_bar_t *status_bar_create(component_t components[], size_t ncomps,
				size_t max_comp_bufsize, const char *divider);
void status_bar_component_update(status_bar_t *sb, size_t posn);
int status_bar_component_get_sleep(status_bar_t *sb, size_t posn);
bool status_bar_component_signal_only(const status_bar_t *sb, size_t posn);
void status_bar_print_on_dirty(status_bar_t *sb, char *buf, size_t bufsize);

#endif
