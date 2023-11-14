/*
 * A multi-threaded status bar.
 */

#define SBAR_MAX_COMP_SIZE 128

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
	const time_t interval;
	const int signum;
} sbar_comp_defn_t;

void sbar_init(const sbar_comp_defn_t *comp_defns, size_t ncomps);
void sbar_update_component(size_t posn);
void sbar_flush_on_dirty(char *buf, size_t bufsize);
void sbar_destroy(void);
