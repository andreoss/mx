#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "screen.h"
#include "term.h"
#include "types.h"

static int passed, failed;
static Parser *parser;

static void check(const char *name, int ok, const char *detail)
{
    if (ok) {
	printf("  PASS  %s\n", name);
	passed++;
    } else {
	printf("  FAIL  %s: %s\n", name, detail);
	failed++;
    }
}

static Term *term_start(int cols, int rows, Palette **pal_out)
{
    static const char *names[] = {
	[255] = 0,[256] = "#cccccc",[257] = "#000000",[258] = "#ffffff",
    };
    Palette *pal = palette_new();
    palette_load(pal, names, LEN(names), 256, 257);
    *pal_out = pal;
    return term_new(cols, rows, pal);
}

static void feed(Term *t, const char *s)
{
    Event ev[512];
    int nev = parser_feed(parser, s, strlen(s), ev, LEN(ev));
    term_process_batch(t, ev, nev);
}

static int row_starts_with(const Term *t, int y, const char *want)
{
    const Screen *s = term_screen(t);
    for (int x = 0; want[x]; x++)
	if (screen_get(s, x, y).r != (Rune) want[x])
	    return 0;
    return 1;
}

static void test_shrink_keeps_cursor_line(void)
{
    Palette *pal;
    Term *t = term_start(12, 8, &pal);
    feed(t, "L1\r\nL2\r\nL3\r\nL4\r\nL5\r\nL6\r\nL7\r\nL8");
    term_resize(t, 12, 4);
    check("shrink keeps cursor line",
	  row_starts_with(t, 3, "L8") && term_cursor_y(t) == 3,
	  "bottom rows were dropped instead of scrolling up");
    term_free(t);
    palette_free(pal);
}

static void test_shrink_grow_is_stable(void)
{
    Palette *pal;
    Term *t = term_start(12, 8, &pal);
    feed(t, "L1\r\nL2\r\nL3\r\nL4\r\nL5\r\nL6\r\nL7\r\nL8");
    for (int i = 0; i < 8; i++) {
	term_resize(t, 12, 3);
	term_resize(t, 12, 8);
    }
    check("repeated shrink and grow keeps content",
	  row_starts_with(t, 0, "L6") && row_starts_with(t, 2, "L8"),
	  "content drains away over repeated resizes");
    term_free(t);
    palette_free(pal);
}

static void test_grow_keeps_cursor(void)
{
    Palette *pal;
    Term *t = term_start(12, 4, &pal);
    feed(t, "L1\r\nL2\r\nL3\r\nL4");
    term_resize(t, 12, 9);
    check("grow keeps rows and cursor",
	  row_starts_with(t, 3, "L4") && term_cursor_y(t) == 3,
	  "rows moved on grow");
    term_free(t);
    palette_free(pal);
}

static void test_cursor_stays_in_bounds(void)
{
    Palette *pal;
    Term *t = term_start(80, 24, &pal);
    int ok = 1;
    srand(4242);
    for (int i = 0; i < 2000 && ok; i++) {
	char buf[64];
	snprintf(buf, sizeof buf, "row %d\r\n", i);
	feed(t, buf);
	int cols = 1 + rand() % 160;
	int rows = 1 + rand() % 60;
	term_resize(t, cols, rows);
	const Screen *s = term_screen(t);
	ok = (int) screen_cols(s) == cols && (int) screen_rows(s) == rows
	    && term_cursor_x(t) >= 0 && term_cursor_x(t) < cols
	    && term_cursor_y(t) >= 0 && term_cursor_y(t) < rows;
    }
    check("randomised resizes keep cursor in bounds", ok, "out of range");
    term_free(t);
    palette_free(pal);
}

static void test_blink_count_on_shrink(void)
{
    Palette *pal;
    Term *t = term_start(80, 5, &pal);
    feed(t, "\033[9G\033[5m");
    for (int i = 0; i < 72; i++)
	feed(t, "B");
    feed(t, "\033[m");
    int before = screen_has_blink(term_screen(t));
    term_resize(t, 8, 5);
    check("dropped columns release blink count",
	  before && !screen_has_blink(term_screen(t)),
	  "blink count survives a column shrink");
    term_free(t);
    palette_free(pal);
}

int main(void)
{
    passed = failed = 0;
    parser = parser_new();
    printf("=== resize tests ===\n");
    test_shrink_keeps_cursor_line();
    test_shrink_grow_is_stable();
    test_grow_keeps_cursor();
    test_cursor_stays_in_bounds();
    test_blink_count_on_shrink();
    parser_free(parser);
    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return !!failed;
}
