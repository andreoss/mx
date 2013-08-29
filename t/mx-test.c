#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../libccor/ccor.h"
#include "parser.h"
#include "screen.h"
#include "term.h"
#include "types.h"
#include "utf8.h"

static void die(const char *msg)
{
    fprintf(stderr, "mx-test: %s\n", msg);
    exit(1);
}

static const char *default_colourname[] = {
    "black", "red3", "green3", "yellow3",
    "blue2", "magenta3", "cyan3", "grey92",
    "grey50", "red", "green", "yellow",
    "#5c5cff", "magenta", "cyan", "white",
    [255] = 0,
    [256] = "#cccccc",
    [257] = "#000000",
    [258] = "#ffffff",
};

static const unsigned int default_fg = 256;
static const unsigned int default_bg = 257;

static char replybuf[8192];
static size_t replylen;

static void write_cb(const char *buf, size_t len, void *user)
{
    UNUSED(user);
    for (size_t i = 0; i < len && replylen < sizeof(replybuf) - 1; i++)
	replybuf[replylen++] = buf[i];
    replybuf[replylen] = '\0';
}

static int show_cursor_marker;
static Palette *dump_pal;

static void dump_reply(const Term *term)
{
    UNUSED(term);
    for (size_t i = 0; i < replylen; i++)
	printf("%02X", (unsigned char) replybuf[i]);
    putchar('\n');
}

static void dump_text(const Term *term)
{
    const Screen *s = term_screen(term);
    int cols = screen_cols(s);
    int rows = screen_rows(s);

    for (int y = 0; y < rows; y++) {
	for (int x = 0; x < cols; x++) {
	    if (show_cursor_marker && x == term->cx && y == term->cy) {
		fwrite("\346\234\250", 1, 3, stdout);
	    } else {
		Cell c = screen_get(s, x, y);
		if (c.r == 0 || c.r == ' ') {
		    putchar(' ');
		} else if (c.r < 128) {
		    putchar((char) c.r);
		} else {
		    char buf[UTF_SIZ];
		    size_t n = utf8_encode(c.r, buf);
		    fwrite(buf, 1, n, stdout);
		}
	    }
	}
	putchar('\n');
    }
}

static uint32_t resolve_colour(uint32_t raw, const Palette *pal)
{
    if (IS_TRUECOLOUR(raw))
	return (TRUERED(raw) << 16) | (TRUEGREEN(raw) << 8) |
	    TRUEBLUE(raw);
    if (pal && raw < PAL_SIZE)
	return palette_get(pal, (int) raw) & 0xFFFFFF;
    return raw & 0xFFFFFF;
}

static void dump_colours(const Term *term, int use_bg)
{
    const Screen *s = term_screen(term);
    int cols = screen_cols(s);
    int rows = screen_rows(s);
    for (int y = 0; y < rows; y++) {
	for (int x = 0; x < cols; x++) {
	    Cell c = screen_get(s, x, y);
	    if (x > 0)
		putchar(' ');
	    printf("%06X", resolve_colour(use_bg ? c.bg : c.fg, dump_pal));
	}
	putchar('\n');
    }
}

static void dump_colour(const Term *t)
{
    dump_colours(t, 1);
}

static void dump_colour_fg(const Term *t)
{
    dump_colours(t, 0);
}

static void dump_colour_underline(const Term *term)
{
    const Screen *s = term_screen(term);
    int cols = screen_cols(s);
    int rows = screen_rows(s);
    for (int y = 0; y < rows; y++) {
	for (int x = 0; x < cols; x++) {
	    Cell c = screen_get(s, x, y);
	    if (x > 0)
		putchar(' ');
	    printf("%06X", resolve_colour(c.ul, dump_pal));
	}
	putchar('\n');
    }
}

static void dump_title(const Term *term)
{
    printf("%s\n", term_title(term));
}

static void dump_mode(const Term *term)
{
    static const struct {
	unsigned attr;
	char ch;
    } mc[] = {
	{ ATTR_REVERSE, 'R' }, { ATTR_BOLD, 'B' }, { ATTR_UNDERLINE, 'U' },
	{ ATTR_FAINT, 'D' }, { ATTR_BLINK, 'K' }, { ATTR_ITALIC, 'I' },
	{ ATTR_INVISIBLE, 'X' }, { ATTR_STRUCK, 'S' },
    };
    const Screen *s = term_screen(term);
    int cols = screen_cols(s);
    int rows = screen_rows(s);
    for (int y = 0; y < rows; y++) {
	for (int x = 0; x < cols; x++) {
	    Cell c = screen_get(s, x, y);
	    char ch = '_';
	    for (size_t i = 0; i < LEN(mc); i++)
		if (c.attr & mc[i].attr) {
		    ch = mc[i].ch;
		    break;
		}
	    putchar(ch);
	    if (x + 1 < cols)
		putchar(' ');
	}
	putchar('\n');
    }
}

int main(int argc, char *argv[])
{
    int cols = 80, rows = 24;
    void (*outs[8])(const Term *);
    int nouts = 0;
    int cursor_marker = 0;

    if (argc > 1) {
	if (sscanf(argv[1], "%dx%d", &cols, &rows) != 2)
	    die("invalid size (expect WxH)");
	if (cols < 1 || rows < 1)
	    die("invalid size dimensions");
    }
    for (int i = 2; i < argc; i++) {
	if (strcmp(argv[i], "cursor-marker") == 0) {
	    cursor_marker = 1;
	    outs[nouts++] = dump_text;
	} else if (strcmp(argv[i], "text") == 0) {
	    outs[nouts++] = dump_text;
	} else if (strcmp(argv[i], "colour") == 0) {
	    outs[nouts++] = dump_colour;
	} else if (strcmp(argv[i], "colour-foreground") == 0) {
	    outs[nouts++] = dump_colour_fg;
	} else if (strcmp(argv[i], "colour-underline") == 0) {
	    outs[nouts++] = dump_colour_underline;
	} else if (strcmp(argv[i], "mode") == 0) {
	    outs[nouts++] = dump_mode;
	} else if (strcmp(argv[i], "title") == 0) {
	    outs[nouts++] = dump_title;
	} else if (strcmp(argv[i], "reply") == 0) {
	    outs[nouts++] = dump_reply;
	} else {
	    fprintf(stderr, "mx-test: unknown output mode %s\n", argv[i]);
	    return 1;
	}
    }
    if (nouts == 0)
	outs[nouts++] = dump_text;

    setlocale(LC_CTYPE, "");
    Palette *pal = palette_new();
    if (!pal)
	die("palette_new failed");
    palette_load(pal, default_colourname, LEN(default_colourname),
		 default_fg, default_bg);
    dump_pal = pal;

    Term *term = term_new(cols, rows, pal);
    if (!term)
	die("term_new failed");
    show_cursor_marker = cursor_marker;
    term_set_write_fn(term, write_cb, NULL);

    Parser *parser = parser_new();
    if (!parser)
	die("parser_new failed");

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
	Event ev[512];
	size_t off = 0;
	while (off < n) {
	    size_t chunk = n - off;
	    if (chunk > 400)
		chunk = 400;
	    if (off + chunk < n)
		while (chunk > 1
		       && ((unsigned char) buf[off + chunk] & 0xC0) ==
		       0x80)
		    chunk--;
	    int nev = parser_feed(parser, buf + off, chunk, ev, LEN(ev));
	    term_process_batch(term, ev, nev);
	    for (int j = 0; j < nev; j++) {
		if (ev[j].type == EVENT_OSC || ev[j].type == EVENT_DCS
		    || ev[j].type == EVENT_APC || ev[j].type == EVENT_PM
		    || ev[j].type == EVENT_SOS)
		    free(ev[j].data.str.buf);
	    }
	    off += chunk;
	}
    }

    for (int i = 0; i < nouts; i++) {
	if (i > 0)
	    fputs("\n\n", stdout);
	outs[i](term);
    }

    term_free(term);
    parser_free(parser);
    palette_free(pal);
    return 0;
}
