#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "control.h"
#include "term.h"
#include "utf8.h"


static const Cell CELL_SPACE = { .r = ' ', .fg = PAL_DEFAULT_FG,
				 .bg = PAL_DEFAULT_BG,
				 .ul = PAL_DEFAULT_FG };

int cursor_shape_blinks(int shape)
{
    return shape > 0 && (shape & 1);
}

Term *term_new(int cols, int rows, Palette *pal)
{
    Term *t = calloc(1, sizeof(*t));
    int i;

    if (!t)
	return NULL;

    t->screen = screen_new(cols, rows);
    t->pal = pal;
    t->top = 0;
    t->bot = rows - 1;
    t->ntabstops = cols;
    t->tabstops = calloc(cols, sizeof(int));


    for (i = TABS; i < cols; i += TABS)
	t->tabstops[i] = 1;

    t->charset_table[0] = CS_USA;
    t->charset_table[1] = CS_USA;
    t->charset_table[2] = CS_USA;
    t->charset_table[3] = CS_USA;

    t->curcell = CELL_SPACE;
    t->save_curcell[0] = CELL_SPACE;
    t->save_curcell[1] = CELL_SPACE;
    t->cursor_shape = CURSOR_SHAPE_BAR;
    t->mode = MODE_WRAP;
    t->flags |= TERM_ALLOW_ALT_SCREEN;

    snprintf(t->title, sizeof(t->title), "term");

    return t;
}

void term_free(Term *t)
{
    if (!t)
	return;
    screen_free(t->screen);
    free(t->tabstops);
    free(t);
}

void term_set_write_fn(Term *t, TermWriteFn fn, void *userdata)
{
    t->write_fn = fn;
    t->write_ctx = userdata;
}

void term_set_allow_alt_screen(Term *t, int allow)
{
    t->flags = flag_set(t->flags, TERM_ALLOW_ALT_SCREEN, allow);
}

void term_set_bell_fn(Term *t, TermBellFn fn, void *userdata)
{
    t->bell_fn = fn;
    t->bell_ctx = userdata;
}

void term_set_title_fn(Term *t, TermTitleFn fn, void *userdata)
{
    t->title_fn = fn;
    t->title_ctx = userdata;
}

void term_bell(Term *t)
{
    if (t->bell_fn)
	t->bell_fn(t->bell_ctx);
}

void term_write(Term *t, const char *buf, size_t len)
{
    if (t->write_fn)
	t->write_fn(buf, len, t->write_ctx);
}

void term_process_batch(Term *t, const Event *ev, int nev)
{
    control_dispatch_batch(t, ev, nev);
}



void term_resize(Term *t, int cols, int rows)
{
    int old_ntabstops = t->ntabstops;
    screen_resize(t->screen, cols, rows);
    t->ntabstops = cols;
    if (cols > old_ntabstops) {
	int *new_tabstops = realloc(t->tabstops,
				    (size_t) cols * sizeof(int));
	if (new_tabstops) {
	    memset(new_tabstops + old_ntabstops, 0,
		   (size_t) (cols - old_ntabstops) * sizeof(int));
	    t->tabstops = new_tabstops;
	}
    }

    t->top = 0;
    t->bot = rows - 1;
    LIMIT(t->cx, 0, cols - 1);
    LIMIT(t->cy, 0, rows - 1);
}



void term_reset(Term *t)
{
    screen_clear(t->screen);
    t->cx = t->cy = 0;
    t->curcell = CELL_SPACE;
    t->mode = MODE_WRAP;
    t->top = 0;
    t->bot = screen_rows(t->screen) - 1;
    t->cur_charset = 0;
    memset(t->tabstops, 0, t->ntabstops * sizeof(int));
    for (int i = TABS; i < t->ntabstops; i += TABS)
	t->tabstops[i] = 1;
    t->cursor_state = CURSOR_DEFAULT;
    t->cursor_shape = CURSOR_SHAPE_BAR;
    t->last_rune = ' ';
}



const Screen *term_screen(const Term *t)
{
    return t->screen;
}

void term_dirty(Term *t)
{
    screen_dirty_all(t->screen);
}

int term_mode(const Term *t, TermMode mode)
{
    return t->mode & mode;
}

int term_mode_raw(const Term *t)
{
    return t->mode;
}

void term_set_mode(Term *t, int set, TermMode mode)
{
    MODBIT(t->mode, set, mode);
}

int term_cursor_x(const Term *t)
{
    return t->cx;
}

int term_cursor_y(const Term *t)
{
    return t->cy;
}

int term_cursor_shape(const Term *t)
{
    return t->cursor_shape;
}

const char *term_title(const Term *t)
{
    return t->title;
}




static int is_word_char(Rune r)
{
    return (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') ||
	(r >= '0' && r <= '9') || r == '_';
}

static Selection selection;

void term_sel_start(Term *t, int x, int y, SelectionSnap snap)
{
    selection.flags |= SELECTION_ACTIVE;
    selection.origin_x = x;
    selection.origin_y = y;
    selection.start_x = x;
    selection.start_y = y;
    selection.end_x = x;
    selection.end_y = y;
    if (snap == SNAP_WORD) {
	Cell cur = screen_get(t->screen, x, y);
	if (is_word_char(cur.r)) {
	    while (selection.start_x > 0) {
		Cell c = screen_get(t->screen, selection.start_x - 1,
				    selection.start_y);
		if (!is_word_char(c.r))
		    break;
		selection.start_x--;
	    }
	    while (selection.end_x < (int)screen_cols(t->screen) - 1) {
		Cell c = screen_get(t->screen, selection.end_x + 1,
				    selection.start_y);
		if (!is_word_char(c.r))
		    break;
		selection.end_x++;
	    }
	}
    }
}

void term_sel_extend(Term *t, int x, int y)
{
    if (!(selection.flags & SELECTION_ACTIVE))
	return;
    if (y < selection.origin_y || (y == selection.origin_y && x < selection.origin_x)) {
	selection.start_x = x;
	selection.start_y = y;
	selection.end_x = selection.origin_x;
	selection.end_y = selection.origin_y;
    } else {
	selection.start_x = selection.origin_x;
	selection.start_y = selection.origin_y;
	selection.end_x = x;
	selection.end_y = y;
    }
}

void term_sel_clear(Term *t)
{
    selection.flags &= ~SELECTION_ACTIVE;
}

int term_sel_active(const Term *t)
{
    return selection.flags & SELECTION_ACTIVE;
}

char *term_sel_get(const Term *t)
{

    int x, y;
    size_t len = 0, cap = 4096;
    char *buf = malloc(cap);
    if (!buf)
	return NULL;
    buf[0] = '\0';

    for (y = selection.start_y; y <= selection.end_y; y++) {
	int x1 = (y == selection.start_y) ? selection.start_x : 0;
	int x2 = (y == selection.end_y) ? selection.end_x
	    : screen_cols(t->screen) - 1;
	for (x = x1; x <= x2; x++) {
	    Cell c = screen_get(t->screen, x, y);
	    char tmp[UTF_SIZ];
	    size_t clen = utf8_encode(c.r, tmp);
	    if (len + clen + 1 > cap) {
		cap *= 2;
		char *new_buf = realloc(buf, cap);
		if (!new_buf) {
		    free(buf);
		    return NULL;
		}
		buf = new_buf;
	    }
	    memcpy(buf + len, tmp, clen);
	    len += clen;
	}
	if (len + 1 > cap) {
	    cap *= 2;
	    char *new_buf = realloc(buf, cap);
	    if (!new_buf) {
		free(buf);
		return NULL;
	    }
	    buf = new_buf;
	}
	buf[len++] = '\n';
    }
    if (len > 0)
	buf[len] = '\0';
    return buf;
}

void
term_sel_get_bounds(const Term *t, int *active, int *start_x, int *start_y,
		    int *end_x, int *end_y)
{
    UNUSED(t);
    if (active)
	*active = selection.flags & SELECTION_ACTIVE;
    if (start_x)
	*start_x = selection.start_x;
    if (start_y)
	*start_y = selection.start_y;
    if (end_x)
	*end_x = selection.end_x;
    if (end_y)
	*end_y = selection.end_y;
}
