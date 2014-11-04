#include <stdlib.h>
#include <string.h>
#include "libccor/ccor.h"
#include "screen.h"

static void screen_dirty(Screen *s, int x1, int y1, int x2, int y2);

static int cell_has_blink(Cell c)
{
    return (c.attr & ATTR_BLINK) != 0;
}

size_t screen_cols(const Screen *s)
{
    return s->cols;
}

size_t screen_rows(const Screen *s)
{
    return s->rows;
}

Cell screen_get(const Screen *s, int x, int y)
{
    if (x < 0 || y < 0 || (size_t) x >= s->cols || (size_t) y >= s->rows)
	return s->filler;
    const Line *buf = (s->flags & SCREEN_ACTIVE) ? s->alt : s->line;
    return buf[y][x];
}

static void screen_dirty_union(Screen *s, int x1, int y1, int x2, int y2)
{
    if (!(s->flags & SCREEN_HAS_DIRTY)) {
	s->dirty_x1 = x1;
	s->dirty_y1 = y1;
	s->dirty_x2 = x2;
	s->dirty_y2 = y2;
	s->flags |= SCREEN_HAS_DIRTY;
    } else {
	if (x1 < s->dirty_x1)
	    s->dirty_x1 = x1;
	if (y1 < s->dirty_y1)
	    s->dirty_y1 = y1;
	if (x2 > s->dirty_x2)
	    s->dirty_x2 = x2;
	if (y2 > s->dirty_y2)
	    s->dirty_y2 = y2;
    }
}

void screen_set(Screen *s, int x, int y, Cell c)
{
    if (x < 0 || y < 0 || (size_t) x >= s->cols || (size_t) y >= s->rows)
	return;
    Line *buf = (s->flags & SCREEN_ACTIVE) ? s->alt : s->line;
    Cell *slot = &buf[y][x];
    if (s->blink_count || (c.attr & ATTR_BLINK)) {
	int was_blink = (slot->attr & ATTR_BLINK) != 0;
	int now_blink = (c.attr & ATTR_BLINK) != 0;
	if (now_blink != was_blink)
	    s->blink_count += now_blink - was_blink;
    }
    if ((size_t) x >= s->used[y])
	s->used[y] = (size_t) x + 1;
    screen_dirty_union(s, x, y, x, y);
    s->dirty_gen++;
    *slot = c;
}

Screen *screen_new(size_t cols, size_t rows)
{
    Screen *s;

    s = calloc(1, sizeof(*s));
    if (!s)
	return NULL;

    s->cols = cols;
    s->rows = rows;
    s->filler = (Cell) {
    .r = ' ', .fg = PAL_DEFAULT_FG, .bg = PAL_DEFAULT_BG, .ul = PAL_DEFAULT_FG};
    s->line = calloc(rows, sizeof(Line));
    s->alt = calloc(rows, sizeof(Line));
    s->used = calloc(rows, sizeof(size_t));

    for (size_t i = 0; i < rows; i++) {
	s->line[i] = malloc((size_t) cols * sizeof(Cell));
	s->alt[i] = malloc((size_t) cols * sizeof(Cell));
	if (!s->line[i])
	    s->line[i] = calloc(1, (size_t) cols * sizeof(Cell));
	if (!s->alt[i])
	    s->alt[i] = calloc(1, (size_t) cols * sizeof(Cell));
	for (size_t j = 0; j < cols; j++) {
	    s->line[i][j] = s->filler;
	    s->alt[i][j] = s->filler;
	}
    }

    screen_dirty_all(s);

    return s;
}

void screen_free(Screen *s)
{
    if (!s)
	return;

    for (size_t i = 0; i < s->rows; i++) {
	free(s->line[i]);
	free(s->alt[i]);
    }

    free(s->line);
    free(s->alt);
    free(s->used);
    free(s);
}

static Line *curbuf(Screen *s)
{
    return (s->flags & SCREEN_ACTIVE) ? s->alt : s->line;
}

static void fill_cells(Screen *s, Line buf, size_t x0, size_t n)
{
    if (s->blink_count)
	for (size_t k = 0; k < n; k++) {
	    s->blink_count -= cell_has_blink(buf[x0 + k]);
	    buf[x0 + k] = s->filler;
	}
    else
	for (size_t k = 0; k < n; k++)
	    buf[x0 + k] = s->filler;
}

void screen_clear(Screen *s)
{
    s->border_gen++;
    for (size_t y = 0; y < s->rows; y++) {
	fill_cells(s, curbuf(s)[y], 0, s->used[y]);
	s->used[y] = 0;
    }
    screen_dirty_all(s);
}

void
screen_clear_region(Screen *s, int x1, int y1, int x2, int y2, Cell filler)
{
    s->border_gen++;
    LIMIT(x1, 0, s->cols - 1);
    LIMIT(y1, 0, s->rows - 1);
    LIMIT(x2, 0, s->cols - 1);
    LIMIT(y2, 0, s->rows - 1);

    for (int y = y1; y <= y2; y++)
	for (int x = x1; x <= x2; x++)
	    curbuf(s)[y][x] = filler;
    screen_dirty(s, x1, y1, x2, y2);
}

void screen_put_cells(Screen *s, int x, int y, const Cell *cells, size_t n)
{
    if (y < 0 || x < 0 || (size_t) y >= s->rows || (size_t) x >= s->cols
	|| n == 0)
	return;
    if (x + n > s->cols)
	n = s->cols - x;

    Line *buf = (s->flags & SCREEN_ACTIVE) ? s->alt : s->line;
    Cell *slot = &buf[y][x];
    int has_blink = s->blink_count > 0;
    if (!has_blink) {
	Attr acc = 0;
	for (size_t k = 0; k < n; k++)
	    acc |= cells[k].attr;
	has_blink = (acc & ATTR_BLINK) != 0;
    }
    if (has_blink) {
	for (size_t k = 0; k < n; k++) {
	    int was = (slot[k].attr & ATTR_BLINK) != 0;
	    int now = (cells[k].attr & ATTR_BLINK) != 0;
	    if (now != was)
		s->blink_count += now - was;
	    slot[k] = cells[k];
	}
    } else {
	memcpy(slot, cells, n * sizeof(Cell));
    }

    if (x + n > (int) s->used[y])
	s->used[y] = (size_t) x + n;

    screen_dirty(s, x, y, x + (int) n - 1, y);
}

void screen_scroll_up(Screen *s, int top, int bot, size_t n)
{
    Line *buf = curbuf(s);

    if (top > bot || n == 0)
	return;
    if (n > (size_t) (bot - top + 1))
	n = (size_t) (bot - top + 1);

    s->border_gen++;

    for (int i = top; i <= bot - (int) n; i++) {
	Line tmp = buf[i];
	buf[i] = buf[i + (int) n];
	buf[i + (int) n] = tmp;
	size_t tmp_used = s->used[i];
	s->used[i] = s->used[i + (int) n];
	s->used[i + (int) n] = tmp_used;
    }

    for (int i = bot - (int) n + 1; i <= bot; i++) {
	fill_cells(s, buf[i], 0, s->used[i]);
	s->used[i] = 0;
    }

    screen_dirty(s, 0, top, s->cols - 1, bot);
}

void screen_scroll_down(Screen *s, int top, int bot, size_t n)
{
    Line *buf = curbuf(s);

    if (top > bot || n == 0)
	return;
    if (n > (size_t) (bot - top + 1))
	n = (size_t) (bot - top + 1);

    s->border_gen++;

    for (int i = bot; i >= top + (int) n; i--) {
	Line tmp = buf[i];
	buf[i] = buf[i - (int) n];
	buf[i - (int) n] = tmp;
	size_t tmp_used = s->used[i];
	s->used[i] = s->used[i - (int) n];
	s->used[i - (int) n] = tmp_used;
    }

    for (int i = top; i < top + (int) n && i <= bot; i++) {
	fill_cells(s, buf[i], 0, s->used[i]);
	s->used[i] = 0;
    }

    screen_dirty(s, 0, top, s->cols - 1, bot);
}

void screen_insert_blank(Screen *s, int x, int y, size_t n)
{
    Line buf = curbuf(s)[y];
    s->border_gen++;
    if (n > (size_t) (s->cols - x))
	n = (size_t) (s->cols - x);
    for (int j = (int) (s->cols - n); j < (int) s->cols; j++)
	s->blink_count -= cell_has_blink(buf[j]);
    memmove(&buf[x + n], &buf[x], (s->cols - x - n) * sizeof(Cell));
    for (size_t j = x; j < x + n; j++)
	buf[j] = s->filler;
    if (x < (int) s->used[y])
	s->used[y] = MIN(s->cols, s->used[y] + n);
    screen_dirty(s, x, y, s->cols - 1, y);
}

void screen_delete_char(Screen *s, int x, int y, size_t n)
{
    Line buf = curbuf(s)[y];
    s->border_gen++;
    if (n > (size_t) (s->cols - x))
	n = (size_t) (s->cols - x);
    for (size_t j = x; j < x + n; j++)
	s->blink_count -= cell_has_blink(buf[j]);
    memmove(&buf[x], &buf[x + n], (s->cols - x - n) * sizeof(Cell));
    for (int j = (int) (s->cols - n); j < (int) s->cols; j++)
	buf[j] = s->filler;
    if (x < (int) s->used[y]) {
	size_t dec = n < s->used[y] ? s->used[y] - n : 0;
	if (dec < (size_t) x)
	    dec = (size_t) x;
	s->used[y] = dec;
    }
    screen_dirty(s, x, y, s->cols - 1, y);
}

void screen_insert_lines(Screen *s, int y, int top, int bot, size_t n)
{
    if (y < top || y > bot)
	return;
    if (n > (size_t) (bot - y + 1))
	n = (size_t) (bot - y + 1);
    screen_scroll_down(s, y, bot, n);
}

void screen_delete_lines(Screen *s, int y, int top, int bot, size_t n)
{
    if (y < top || y > bot)
	return;
    if (n > (size_t) (bot - y + 1))
	n = (size_t) (bot - y + 1);
    screen_scroll_up(s, y, bot, n);
}

void screen_resize(Screen *s, size_t cols, size_t rows)
{
    Line *new_line, *new_alt;

    if (cols < 1 || rows < 1)
	return;

    s->border_gen++;

    size_t minrow = MIN(rows, s->rows);
    size_t mincol = MIN(cols, s->cols);

    size_t *new_used = calloc(rows, sizeof(size_t));
    new_line = calloc(rows, sizeof(Line));
    new_alt = calloc(rows, sizeof(Line));
    if (!new_line || !new_alt) {
	free(new_used);
	free(new_line);
	free(new_alt);
	return;
    }

    for (size_t i = 0; i < minrow; i++) {
	new_used[i] = MIN(s->used[i], cols);
	Cell *tmp;
	tmp = realloc(s->line[i], cols * sizeof(Cell));
	new_line[i] = tmp ? tmp : s->line[i];
	tmp = realloc(s->alt[i], cols * sizeof(Cell));
	new_alt[i] = tmp ? tmp : s->alt[i];
	if (cols > mincol)
	    for (size_t j = mincol; j < cols; j++) {
		new_line[i][j] = s->filler;
		new_alt[i][j] = s->filler;
	    }
    }

    for (size_t i = minrow; i < rows; i++) {
	new_line[i] = malloc(cols * sizeof(Cell));
	if (!new_line[i])
	    new_line[i] = calloc(1, cols * sizeof(Cell));
	new_alt[i] = malloc(cols * sizeof(Cell));
	if (!new_alt[i])
	    new_alt[i] = calloc(1, cols * sizeof(Cell));
	for (size_t j = 0; j < cols; j++) {
	    new_line[i][j] = s->filler;
	    new_alt[i][j] = s->filler;
	}
    }

    for (size_t i = rows; i < s->rows; i++) {
	for (size_t j = 0; j < s->cols; j++) {
	    s->blink_count -= cell_has_blink(s->line[i][j]);
	    s->blink_count -= cell_has_blink(s->alt[i][j]);
	}
	free(s->line[i]);
	free(s->alt[i]);
    }

    free(s->line);
    free(s->alt);
    free(s->used);
    s->line = new_line;
    s->alt = new_alt;
    s->used = new_used;

    s->cols = cols;
    s->rows = rows;

    screen_dirty_all(s);
}

void screen_swap(Screen *s)
{
    s->flags ^= SCREEN_ACTIVE;
    s->border_gen++;
    screen_dirty_all(s);
}

int screen_is_alt(const Screen *s)
{
    return s->flags & SCREEN_ACTIVE;
}

static void screen_dirty(Screen *s, int x1, int y1, int x2, int y2)
{
    if (x1 > x2) {
	int t = x1;
	x1 = x2;
	x2 = t;
    }
    if (y1 > y2) {
	int t = y1;
	y1 = y2;
	y2 = t;
    }
    LIMIT(x1, 0, s->cols - 1);
    LIMIT(y1, 0, s->rows - 1);
    LIMIT(x2, 0, s->cols - 1);
    LIMIT(y2, 0, s->rows - 1);
    s->dirty_gen++;
    screen_dirty_union(s, x1, y1, x2, y2);
}

void screen_dirty_all(Screen *s)
{
    s->dirty_gen++;
    screen_dirty_union(s, 0, 0, (int) s->cols - 1, (int) s->rows - 1);
}

int screen_border_gen(const Screen *s)
{
    return s->border_gen;
}

int screen_dirty_get(const Screen *s, int *x1, int *y1, int *x2, int *y2)
{
    if (s->flags & SCREEN_HAS_DIRTY) {
	*x1 = s->dirty_x1;
	*y1 = s->dirty_y1;
	*x2 = s->dirty_x2;
	*y2 = s->dirty_y2;
	return 1;
    }
    return 0;
}

void screen_clean(Screen *s)
{
    s->flags &= ~SCREEN_HAS_DIRTY;
}

int screen_has_blink(const Screen *s)
{
    return s->blink_count > 0;
}
