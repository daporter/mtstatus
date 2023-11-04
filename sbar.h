/*
 * A multi-threaded status bar.
 */

#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Function that returns an updated value for a status bar component.
 */
typedef void (*sbar_updater_t)(char *buf, const int bufsize, const char *args,
			       const char *no_val_str);

/*
 * Status bar component.
 */
typedef struct {
	const sbar_updater_t update;
	const char *args;
	const int sleep_secs;
	const int signum;
} sbar_cmp_t;

void sbar_init(const sbar_cmp_t *components, size_t ncomps,
	       size_t max_cmp_bufsize, const char *divider,
	       const char *no_val_str);
void sbar_cmp_update(size_t posn);
int sbar_cmp_get_sleep(size_t posn);
bool sbar_cmp_is_signal_only(size_t posn);
void sbar_render_on_dirty(char *buf, size_t bufsize);
void sbar_destroy(void);

#endif
