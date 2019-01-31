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

static void test_erase_display_releases_blink(void)
{
    Palette *pal;
    Term *t = term_start(20, 4, &pal);
    feed(t, "\033[5mblink\033[m");
    int before = screen_has_blink(term_screen(t));
    feed(t, "\033[2J");
    check("erase display releases blink count",
	  before && !screen_has_blink(term_screen(t)),
	  "blink count survives ED");
    term_free(t);
    palette_free(pal);
}

static void test_erase_line_releases_blink(void)
{
    Palette *pal;
    Term *t = term_start(20, 4, &pal);
    feed(t, "\033[5mblink\033[m\r");
    int before = screen_has_blink(term_screen(t));
    feed(t, "\033[K");
    check("erase line releases blink count",
	  before && !screen_has_blink(term_screen(t)),
	  "blink count survives EL");
    term_free(t);
    palette_free(pal);
}

static void test_decaln_tracks_blink(void)
{
    Palette *pal;
    Term *t = term_start(20, 4, &pal);
    feed(t, "\033[5m\033#8");
    int after_aln = screen_has_blink(term_screen(t));
    feed(t, "\033[m\033[2J");
    check("decaln fill tracks blink count",
	  after_aln && !screen_has_blink(term_screen(t)),
	  "blink count is wrong after a blinking DECALN fill");
    term_free(t);
    palette_free(pal);
}

int main(void)
{
    passed = failed = 0;
    parser = parser_new();
    printf("=== screen tests ===\n");
    test_erase_display_releases_blink();
    test_erase_line_releases_blink();
    test_decaln_tracks_blink();
    parser_free(parser);
    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return !!failed;
}
