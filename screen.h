#ifndef SCREEN_H
#define SCREEN_H
#include "types.h"


enum {
    SCREEN_ACTIVE = 1 << 0,
    SCREEN_HAS_DIRTY = 1 << 1,
};

struct Screen {
    size_t cols, rows;
    Line *line;
    Line *alt;
    int dirty_x1, dirty_y1, dirty_x2, dirty_y2;
    int dirty_gen;
    int border_gen;

    unsigned flags;

    size_t *used;

    Cell filler;

    int blink_count;
};

typedef struct Screen Screen;

size_t screen_cols(const Screen *s);
size_t screen_rows(const Screen *s);
Cell screen_get(const Screen *s, int x, int y);
void screen_set(Screen *s, int x, int y, Cell c);


Screen *screen_new(size_t cols, size_t rows);
void screen_free(Screen *s);


void screen_resize(Screen *s, size_t cols, size_t rows);


void screen_clear(Screen *s);
void screen_clear_region(Screen *s, int x1, int y1, int x2, int y2,
			 Cell filler);
void screen_put_cells(Screen *s, int x, int y, const Cell *cells,
		      size_t n);
void screen_scroll_up(Screen *s, int top, int bot, size_t n);
void screen_scroll_down(Screen *s, int top, int bot, size_t n);
void screen_insert_blank(Screen *s, int x, int y, size_t n);
void screen_delete_char(Screen *s, int x, int y, size_t n);
void screen_insert_lines(Screen *s, int y, int top, int bot, size_t n);
void screen_delete_lines(Screen *s, int y, int top, int bot, size_t n);


void screen_swap(Screen *s);
int screen_is_alt(const Screen *s);


void screen_dirty_all(Screen *s);
int screen_border_gen(const Screen *s);
int screen_dirty_get(const Screen *s, int *x1, int *y1, int *x2, int *y2);
void screen_clean(Screen *s);


int screen_has_blink(const Screen *s);
#endif
