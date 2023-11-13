#include "sbar.h"

#include <assert.h>
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "errors.h"
#include "util.h"

typedef struct {
	sbar_updater_t update;
	const char *args;
	int signum;
	char *buf;
} component_t;

/*
 * Invariant: A status bar is "dirty" iff any of its component buffers have been
 * updated since the last render.
 */

static const char divider[] = "  ";
static const char no_val_str[] = "n/a";
static char *component_bufs;
static component_t *components;
static size_t ncomponents;
static bool dirty = false;
static pthread_mutex_t dirty_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dirty_cnd = PTHREAD_COND_INITIALIZER;

static_assert(LEN(divider) <= SBAR_MAX_COMP_SIZE,
	      "divider must be no bigger than component length");

void sbar_init(const sbar_component_defn_t *comp_defns, const size_t ncomps)
{
	component_bufs = Calloc(ncomps * SBAR_MAX_COMP_SIZE, sizeof(char));
	components = Calloc(ncomps, sizeof(component_t));
	for (size_t i = 0; i < ncomps; i++) {
		components[i].update = comp_defns[i].update;
		components[i].args = comp_defns[i].args;
		components[i].signum = comp_defns[i].signum;
		components[i].buf = component_bufs + (SBAR_MAX_COMP_SIZE * i);
		Strcpy(components[i].buf, no_val_str);
	}
	ncomponents = ncomps;
}

void sbar_update_component(const size_t posn)
{
	assert(posn < ncomponents &&
	       "posn cannot be greater than number of component buffers");

	const component_t c = components[posn];
	char tmpbuf[SBAR_MAX_COMP_SIZE];

	c.update(tmpbuf, SBAR_MAX_COMP_SIZE, c.args, no_val_str);

	/*
	 * Maintain status bar "dirty" invariant.
	 */
	Pthread_mutex_lock(&dirty_mtx);
	Memcpy(c.buf, tmpbuf, SBAR_MAX_COMP_SIZE);
	dirty = true;
	Pthread_mutex_unlock(&dirty_mtx);

	Pthread_cond_signal(&dirty_cnd);
}

void sbar_render_on_dirty(char *buf, const size_t bufsize)
{
	size_t i = 0;
	char *ptr = buf;
	char *end = buf + bufsize;
	const char *cbuf;

	bzero(buf, bufsize);

	/*
	 * Maintain status bar "dirty" invariant.
	 */
	Pthread_mutex_lock(&dirty_mtx);
	while (!dirty)
		Pthread_cond_wait(&dirty_cnd, &dirty_mtx);

	for (i = 0; (ptr < end) && (i < ncomponents - 1); i++) {
		cbuf = components[i].buf;
		if (strlen(cbuf) > 0) {
			ptr = util_cat(ptr, end, cbuf);
			ptr = util_cat(ptr, end, divider);
		}
	}
	if (ptr < end) {
		cbuf = components[i].buf;
		if (strlen(cbuf) > 0)
			ptr = util_cat(ptr, end, cbuf);
	}

	dirty = false;
	Pthread_mutex_unlock(&dirty_mtx);
}

void sbar_destroy(void)
{
	if (component_bufs != NULL)
		Free(component_bufs);
}
