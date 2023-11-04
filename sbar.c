#include "sbar.h"

#include <assert.h>
#include <string.h>

#include "errors.h"
#include "util.h"

typedef struct {
	const sbar_cmp_t *components;
	size_t ncomponents;
	char *cmp_bufs;
	size_t max_cmp_bufsize;
	const char *divider;
	const char *no_val_str;
	bool dirty;
	pthread_mutex_t mtx;
	pthread_cond_t dirty_cnd;
} sbar_t;

static sbar_t *sbar;

void sbar_init(const sbar_cmp_t *components, const size_t ncomps,
	       const size_t max_cmp_bufsize, const char *divider,
	       const char *no_val_str)
{
	sbar = Malloc(sizeof *sbar);

	sbar->components = components;
	sbar->ncomponents = ncomps;
	sbar->cmp_bufs = Calloc(ncomps * max_cmp_bufsize, sizeof(char));
	sbar->max_cmp_bufsize = max_cmp_bufsize;
	sbar->divider = divider;
	sbar->no_val_str = no_val_str;
	sbar->dirty = false;
	Pthread_mutex_init(&sbar->mtx, NULL);
	Pthread_cond_init(&sbar->dirty_cnd, NULL);
}

static char *status_bar_get_cmp_buf(const sbar_t *sb, const size_t posn)
{
	assert(posn < sb->ncomponents &&
	       "posn cannot be greater than number of component buffers");

	return sb->cmp_bufs + (sb->max_cmp_bufsize * posn);
}

void sbar_cmp_update(const size_t posn)
{
	assert(posn < sbar->ncomponents &&
	       "posn cannot be greater than number of component buffers");

	const sbar_cmp_t c = sbar->components[posn];
	char tmpbuf[sbar->max_cmp_bufsize];
	char *cbuf = status_bar_get_cmp_buf(sbar, posn);

	c.update(tmpbuf, LEN(tmpbuf), c.args, sbar->no_val_str);

	Pthread_mutex_lock(&sbar->mtx);
	Memcpy(cbuf, tmpbuf, sbar->max_cmp_bufsize);
	sbar->dirty = true;
	Pthread_mutex_unlock(&sbar->mtx);
	Pthread_cond_signal(&sbar->dirty_cnd);
}

int sbar_cmp_get_sleep(size_t posn)
{
	return sbar->components[posn].sleep_secs;
}

bool sbar_cmp_is_signal_only(const size_t posn)
{
	const sbar_cmp_t c = sbar->components[posn];

	return c.sleep_secs < 0 && c.signum >= 0;
}

void sbar_render_on_dirty(char *buf, const size_t bufsize)
{
	size_t i = 0;
	char *ptr = buf;
	char *end = buf + bufsize;
	const char *cb;

	Pthread_mutex_lock(&sbar->mtx);
	while (!sbar->dirty)
		Pthread_cond_wait(&sbar->dirty_cnd, &sbar->mtx);

	for (i = 0; (ptr < end) && (i < sbar->ncomponents - 1); i++) {
		cb = status_bar_get_cmp_buf(sbar, i);
		if (strlen(cb) > 0) {
			ptr = util_cat(ptr, end, cb);
			ptr = util_cat(ptr, end, sbar->divider);
		}
	}
	if (ptr < end) {
		cb = status_bar_get_cmp_buf(sbar, i);
		if (strlen(cb) > 0)
			ptr = util_cat(ptr, end, cb);
	}

	sbar->dirty = false;
	Pthread_mutex_unlock(&sbar->mtx);
}

void sbar_destroy(void)
{
	if (sbar != NULL) {
		if (sbar->cmp_bufs != NULL)
			Free(sbar->cmp_bufs);
		Pthread_mutex_destroy(&sbar->mtx);
		Pthread_cond_destroy(&sbar->dirty_cnd);

		Free(sbar);
	}
}
