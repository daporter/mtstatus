#include "status_bar.h"

#include <assert.h>
#include <pthread.h>
#include <string.h>

#include "errors.h"
#include "util.h"

status_bar_t *status_bar_create(component_t components[], const size_t ncomps,
				const size_t max_comp_bufsize,
				const char *divider)
{
	status_bar_t *sb = Malloc(sizeof *sb);

	sb->components = components;
	sb->ncomponents = ncomps;
	sb->component_bufs = Calloc(ncomps * max_comp_bufsize, sizeof(char));
	sb->max_comp_bufsize = max_comp_bufsize;
	sb->divider = divider;
	sb->dirty = false;
	Pthread_mutex_init(&sb->mtx, NULL);
	Pthread_cond_init(&sb->dirty_cnd, NULL);

	return sb;
}

static char *status_bar_get_component_buf(const status_bar_t *sb,
					  const size_t posn)
{
	assert(posn < sb->ncomponents &&
	       "posn cannot be greater than number of component buffers");

	return sb->component_bufs + (sb->max_comp_bufsize * posn);
}

void status_bar_component_update(status_bar_t *sb, const size_t posn)
{
	assert(posn < sb->ncomponents &&
	       "posn cannot be greater than number of component buffers");

	const component_t c = sb->components[posn];
	char tmpbuf[sb->max_comp_bufsize];
	char *cbuf = status_bar_get_component_buf(sb, posn);

	c.update(tmpbuf, LEN(tmpbuf), c.args);

	Pthread_mutex_lock(&sb->mtx);
	memcpy(cbuf, tmpbuf, sb->max_comp_bufsize);
	sb->dirty = true;
	Pthread_mutex_unlock(&sb->mtx);
	Pthread_cond_signal(&sb->dirty_cnd);
}

int status_bar_component_get_sleep(status_bar_t *sb, size_t posn)
{
	return sb->components[posn].sleep_secs;
}

bool status_bar_component_signal_only(const status_bar_t *sb, const size_t posn)
{
	const component_t c = sb->components[posn];

	return c.sleep_secs < 0 && c.signum >= 0;
}

void status_bar_print_on_dirty(status_bar_t *sb, char *buf,
			       const size_t bufsize)
{
	size_t i = 0;
	char *ptr = buf;
	char *end = buf + bufsize;
	const char *cb;

	Pthread_mutex_lock(&sb->mtx);
	while (!sb->dirty)
		Pthread_cond_wait(&sb->dirty_cnd, &sb->mtx);

	for (i = 0; (ptr < end) && (i < sb->ncomponents - 1); i++) {
		cb = status_bar_get_component_buf(sb, i);
		if (strlen(cb) > 0) {
			ptr = util_cat(ptr, end, cb);
			ptr = util_cat(ptr, end, sb->divider);
		}
	}
	if (ptr < end) {
		cb = status_bar_get_component_buf(sb, i);
		if (strlen(cb) > 0)
			ptr = util_cat(ptr, end, cb);
	}

	sb->dirty = false;
	Pthread_mutex_unlock(&sb->mtx);
}
