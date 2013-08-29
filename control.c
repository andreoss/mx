#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "control.h"
#include "libccor/ccor.h"
#include "screen.h"
#include "term.h"
#include "types.h"
#include "utf8.h"

#define WC_ASCII_MIN  0x20
#define WC_ASCII_MAX  0x7F
#define WC_GLYPH_MIN  0x2190
#define WC_GLYPH_MAX  0x27BF
#define WC_GLYPH2_MIN 0x2900
#define WC_GLYPH2_MAX 0x2BFF
#define WC_CJK_MIN    0x2E80
#define WC_JAMO_MIN   0x1100
#define WC_PUA_MIN    0xE000
#define WC_PUA_MAX    0xF8FF
#define WC_VAR_MIN    0xFE00
#define WC_VAR_MAX    0xFE0F
#define WC_HALF_MIN   0xFE20
#define WC_HALF_MAX   0xFE2F
#define WC_SPEC_MIN   0xFFF0
#define WC_SPEC_MAX   0xFFFF

static int wcwidth_safe(wchar_t u)
{
    if (u >= WC_ASCII_MIN && u < WC_ASCII_MAX)
	return 1;
    if ((u >= WC_GLYPH_MIN && u <= WC_GLYPH_MAX) ||
	(u >= WC_GLYPH2_MIN && u <= WC_GLYPH2_MAX))
	return 1;
    if (u >= WC_CJK_MIN)
	return wcwidth(u);
    {
	int w = wcwidth(u);
	if (w == 2) {
	    if (u < WC_JAMO_MIN)
		return 1;
	    if ((u >= WC_PUA_MIN && u <= WC_PUA_MAX) ||
		(u >= WC_VAR_MIN && u <= WC_VAR_MAX) ||
		(u >= WC_HALF_MIN && u <= WC_HALF_MAX) ||
		(u >= WC_SPEC_MIN && u <= WC_SPEC_MAX))
		return 1;
	}
	if (w < 0)
	    return 1;
	return w;
    }
}

static const char *dec_special_graphics[62] = {
    "↑", "↓", "→", "←", "█", "▚", "☃",
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, " ",
    "◆", "▒", "␉", "␌", "␍", "␊", "°", "±",
    "␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺",
    "⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬",
    "│", "≤", "≥", "π", "≠", "£", "·",
};



static void term_scroll_up(Term * t, int top, int bot, int n);
static void term_scroll_down(Term * t, int top, int bot, int n);
static void term_cursor(Term * t, int mode);

static void term_move_to(Term *t, int x, int y)
{
    int miny, maxy;
    if (t->cursor_state & CURSOR_ORIGIN) {
	miny = t->top;
	maxy = t->bot;
    } else {
	miny = 0;
	maxy = screen_rows(t->screen) - 1;
    }
    int nx = LIMIT(x, 0, screen_cols(t->screen) - 1);
    int ny = LIMIT(y, miny, maxy);
    if (nx == t->cx && ny == t->cy)
	return;
    t->cursor_state &= ~(CURSOR_WRAPNEXT | CURSOR_INPUT_NEEDS_WRAP);
    t->cx = nx;
    t->cy = ny;
}


static void term_move_abs(Term *t, int x, int y)
{
    term_move_to(t, x,
		 y + ((t->cursor_state & CURSOR_ORIGIN) ? t->top : 0));
}

static void term_newline(Term *t, int first_col)
{
    int y = t->cy;
    if (y == t->bot) {
	term_scroll_up(t, t->top, t->bot, 1);
    } else {
	y++;
    }
    term_move_to(t, first_col ? 0 : t->cx, y);
}

static void term_scroll_up(Term *t, int top, int bot, int n)
{
    screen_scroll_up(t->screen, top, bot, n);
}

static void term_scroll_down(Term *t, int top, int bot, int n)
{
    screen_scroll_down(t->screen, top, bot, n);
}

static void term_put_tab(Term *t, int n)
{
    int x = t->cx;
    if (n > 0) {
	while (x < screen_cols(t->screen) - 1 && n--)
	    for (++x; x < screen_cols(t->screen) && !t->tabstops[x]; ++x);
    } else if (n < 0) {
	while (x > 0 && n++)
	    for (--x; x > 0 && !t->tabstops[x]; --x);
    }
    t->cx = LIMIT(x, 0, screen_cols(t->screen) - 1);
}

static void term_set_char(Term *t, Rune u, int x, int y)
{
    if (t->charset_table[t->cur_charset] == CS_DEC_GRAPHICS &&
	BETWEEN(u, 0x41, 0x7e) && dec_special_graphics[u - 0x41]) {
	const char *s = dec_special_graphics[u - 0x41];
	utf8_decode(&s, &u);
    }
    Cell c = t->curcell;
    c.r = u;
    screen_set(t->screen, x, y, c);
}

static void term_clear_region(Term *t, int x1, int y1, int x2, int y2)
{
    int temp;
    if (x1 > x2)
	temp = x1, x1 = x2, x2 = temp;
    if (y1 > y2)
	temp = y1, y1 = y2, y2 = temp;
    Cell filler = t->curcell;
    filler.attr = 0;
    filler.r = ' ';
    screen_clear_region(t->screen, x1, y1, x2, y2, filler);
}

static void term_cursor(Term *t, int mode)
{
    int alt = screen_is_alt(t->screen);
    if (mode == CURSOR_SAVE) {
	t->save_cx[alt] = t->cx;
	t->save_cy[alt] = t->cy;
	t->save_curcell[alt] = t->curcell;
	t->save_cursor_state[alt] = t->cursor_state;
    } else if (mode == CURSOR_LOAD) {
	int x = t->save_cx[alt];
	int y = t->save_cy[alt];
	t->cx = x;
	t->cy = y;
	t->curcell = t->save_curcell[alt];
	t->cursor_state = t->save_cursor_state[alt];
	term_move_to(t, x, y);
    }
}

static void term_insert_blank(Term *t, int n)
{
    screen_insert_blank(t->screen, t->cx, t->cy, n);
}



static int32_t term_def_colour(const int *attr, int *npar, int l)
{
    int32_t idx = -1;
    unsigned int r, g, b;
    if (*npar + 1 >= l)
	return -1;
    switch (attr[*npar + 1]) {
    case 2:
	if (*npar + 4 >= l)
	    break;
	r = attr[*npar + 2];
	g = attr[*npar + 3];
	b = attr[*npar + 4];
	*npar += 4;
	if (r > 255 || g > 255 || b > 255)
	    fprintf(stderr, "erresc: bad rgb colour (%u,%u,%u)\n", r, g,
		    b);
	else
	    idx = TRUECOLOUR(r, g, b);
	break;
    case 5:
	if (*npar + 2 >= l)
	    break;
	*npar += 2;
	if (!BETWEEN(attr[*npar], 0, 255))
	    fprintf(stderr, "erresc: bad fgcolour %d\n", attr[*npar]);
	else
	    idx = attr[*npar];
	break;
    case 0:
    case 1:
    case 3:
    case 4:
    default:
	fprintf(stderr, "erresc(38): gfx attr %d unknown\n", attr[*npar]);
	break;
    }
    return idx;
}



static void term_set_attr(Term *t, const int *attr, int l)
{
    int i;
    int32_t idx;
    if (l == 0) {
	t->curcell.attr &= ~(ATTR_BOLD | ATTR_FAINT | ATTR_ITALIC |
			     ATTR_UNDERLINE | ATTR_BLINK_SLOW |
			     ATTR_BLINK_FAST | ATTR_REVERSE |
			     ATTR_INVISIBLE | ATTR_STRUCK);
	t->curcell.fg = PAL_DEFAULT_FG;
	t->curcell.bg = PAL_DEFAULT_BG;
	t->curcell.ul = PAL_DEFAULT_FG;
	return;
    }
    for (i = 0; i < l; i++) {
	switch (attr[i]) {
	case SGR_RESET:
	    t->curcell.attr &= ~(ATTR_BOLD | ATTR_FAINT | ATTR_ITALIC |
				 ATTR_UNDERLINE | ATTR_BLINK_SLOW |
				 ATTR_BLINK_FAST | ATTR_REVERSE |
				 ATTR_INVISIBLE | ATTR_STRUCK);
	    t->curcell.fg = PAL_DEFAULT_FG;
	    t->curcell.bg = PAL_DEFAULT_BG;
	    t->curcell.ul = PAL_DEFAULT_FG;
	    break;
	case SGR_BOLD:
	    t->curcell.attr |= ATTR_BOLD;
	    break;
	case SGR_FAINT:
	    t->curcell.attr |= ATTR_FAINT;
	    break;
	case SGR_ITALIC:
	    t->curcell.attr |= ATTR_ITALIC;
	    break;
	case SGR_UNDERLINE:
	    t->curcell.attr |= ATTR_UNDERLINE;
	    break;
	case SGR_BLINK_SLOW:
	    t->curcell.attr |= ATTR_BLINK_SLOW;
	    break;
	case SGR_BLINK_FAST:
	    t->curcell.attr |= ATTR_BLINK_FAST;
	    break;
	case SGR_REVERSE:
	    t->curcell.attr |= ATTR_REVERSE;
	    break;
	case SGR_INVISIBLE:
	    t->curcell.attr |= ATTR_INVISIBLE;
	    break;
	case SGR_STRUCK:
	    t->curcell.attr |= ATTR_STRUCK;
	    break;
	case SGR_NORMAL:
	    t->curcell.attr &= ~(ATTR_BOLD | ATTR_FAINT);
	    break;
	case SGR_NOT_ITALIC:
	    t->curcell.attr &= ~ATTR_ITALIC;
	    break;
	case SGR_NOT_UNDERLINE:
	    t->curcell.attr &= ~ATTR_UNDERLINE;
	    break;
	case SGR_NOT_BLINK:
	    t->curcell.attr &= ~(ATTR_BLINK_SLOW | ATTR_BLINK_FAST);
	    break;
	case SGR_NOT_REVERSE:
	    t->curcell.attr &= ~ATTR_REVERSE;
	    break;
	case SGR_NOT_INVISIBLE:
	    t->curcell.attr &= ~ATTR_INVISIBLE;
	    break;
	case SGR_NOT_STRUCK:
	    t->curcell.attr &= ~ATTR_STRUCK;
	    break;
	case SGR_FG_EXTENDED:
	    if ((idx = term_def_colour(attr, &i, l)) != -1)
		t->curcell.fg = idx;
	    break;
	case SGR_DEFAULT_FG:
	    t->curcell.fg = PAL_DEFAULT_FG;
	    break;
	case SGR_BG_EXTENDED:
	    if ((idx = term_def_colour(attr, &i, l)) != -1)
		t->curcell.bg = idx;
	    break;
	case SGR_DEFAULT_BG:
	    t->curcell.bg = PAL_DEFAULT_BG;
	    break;
	case SGR_UL_COLOUR:
	    if ((idx = term_def_colour(attr, &i, l)) != -1)
		t->curcell.ul = idx;
	    break;
	case SGR_DEFAULT_UL:
	    t->curcell.ul = PAL_DEFAULT_FG;
	    break;
	default:
	    if (BETWEEN(attr[i], SGR_FG_BASE, SGR_FG_BASE + ANSI_COLOURS - 1)) {
		t->curcell.fg = attr[i] - SGR_FG_BASE;
	    } else if (BETWEEN(attr[i], SGR_BG_BASE, SGR_BG_BASE + ANSI_COLOURS - 1)) {
		t->curcell.bg = attr[i] - SGR_BG_BASE;
	    } else if (BETWEEN(attr[i], SGR_FG_BRIGHT, SGR_FG_BRIGHT + ANSI_COLOURS - 1)) {
		t->curcell.fg = attr[i] - SGR_FG_BRIGHT + ANSI_COLOURS;
	    } else if (BETWEEN(attr[i], SGR_BG_BRIGHT, SGR_BG_BRIGHT + ANSI_COLOURS - 1)) {
		t->curcell.bg = attr[i] - SGR_BG_BRIGHT + ANSI_COLOURS;
	    } else {
		fprintf(stderr, "erresc(default): gfx attr %d unknown\n",
			attr[i]);
	    }
	    break;
	}
    }
}



static void
csi_set_mode(Term *t, int priv, int set, const int *args, int narg)
{
    int alt;
    const int *lim;
    for (lim = args + narg; args < lim; ++args) {
	if (priv) {
	    switch (*args) {
	    case 1:
		MODBIT(t->mode, set, MODE_APPCURSOR);
		break;
	    case 2:
		break;
	    case 3:
		break;
	    case 4:
		break;
	    case 6:
		MODBIT(t->cursor_state, set, CURSOR_ORIGIN);
		term_move_abs(t, 0, 0);
		break;
	    case 7:
		MODBIT(t->mode, set, MODE_WRAP);
		break;
	    case 8:
		break;
	    case 9:
		t->mode &= ~MODE_MOUSE;
		MODBIT(t->mode, set, MODE_MOUSEX10);
		break;
	    case 12:
		break;
	    case 18:
		break;
	    case 19:
		break;
	    case 25:
		MODBIT(t->mode, !set, MODE_HIDE);
		break;
	    case 42:
		break;
	    case 1049:
		if (!(t->flags & TERM_ALLOW_ALT_SCREEN))
		    break;
		term_cursor(t, (set) ? CURSOR_SAVE : CURSOR_LOAD);
		__attribute__((fallthrough));
	    case 47:
	    case 1047:
		if (!(t->flags & TERM_ALLOW_ALT_SCREEN))
		    break;
		alt = screen_is_alt(t->screen);
		if (alt)
		    term_clear_region(t, 0, 0,
				      screen_cols(t->screen) - 1,
				      screen_rows(t->screen) - 1);
		if (set ^ alt)
		    screen_swap(t->screen);
		if (*args != 1049)
		    break;
		__attribute__((fallthrough));
	    case 1048:
		term_cursor(t, (set) ? CURSOR_SAVE : CURSOR_LOAD);
		break;
	    case 1000:
		t->mode &= ~MODE_MOUSE;
		MODBIT(t->mode, set, MODE_MOUSEBTN);
		break;
	    case 1002:
		t->mode &= ~MODE_MOUSE;
		MODBIT(t->mode, set, MODE_MOUSEMOTION);
		break;
	    case 1003:
		t->mode &= ~MODE_MOUSE;
		MODBIT(t->mode, set, MODE_MOUSEMANY);
		break;
	    case 1006:
		MODBIT(t->mode, set, MODE_MOUSESGR);
		break;
	    case 1004:
		MODBIT(t->mode, set, MODE_FOCUS);
		break;
	    case 2004:
		MODBIT(t->mode, set, MODE_BRACKETED_PASTE);
		break;
	    case 2026:
	    case 8452:
		break;
	    case 1001:
	    case 1005:
	    case 1015:
		break;
	    case 2031:
	    case 7727:
		break;
	    default:
		fprintf(stderr,
			"erresc: unknown private set/reset mode %d\n",
			*args);
		break;
	    }
	} else {
	    switch (*args) {
	    case 0:
		break;
	    case 4:
		MODBIT(t->mode, set, MODE_INSERT);
		break;
	    case 20:
		MODBIT(t->mode, set, MODE_CRLF);
		break;
	    default:
		fprintf(stderr, "erresc: unknown set/reset mode %d\n",
			*args);
		break;
	    }
	}
    }
}



static void term_putc(Term *t, Rune u)
{
    int width = wcwidth_safe((wchar_t) u);
    int cols = screen_cols(t->screen);

    if ((t->mode & MODE_WRAP)
	&& (t->cursor_state & (CURSOR_WRAPNEXT | CURSOR_INPUT_NEEDS_WRAP))) {
	term_newline(t, 1);
	t->cursor_state &= ~(CURSOR_WRAPNEXT | CURSOR_INPUT_NEEDS_WRAP);
    }
    if ((t->mode & MODE_INSERT) && t->cx + width < cols)
	term_insert_blank(t, 1);

    if (!(t->mode & MODE_WRAP) && t->cx + width > cols)
	term_move_to(t, cols - width, t->cy);

    term_set_char(t, u, t->cx, t->cy);
    t->last_rune = u;

    if (width == 2 && t->cx + 1 < cols) {
	Cell wdummy = { .r = '\0', .attr = ATTR_WDUMMY };
	screen_set(t->screen, t->cx + 1, t->cy, wdummy);
    }
    if (t->cx + width < cols) {
	t->cursor_state &= ~(CURSOR_WRAPNEXT | CURSOR_INPUT_NEEDS_WRAP);
	t->cx += width;
    } else {
	t->cursor_state |= CURSOR_WRAPNEXT;
	if (t->cx + width > cols)
	    t->cursor_state |= CURSOR_INPUT_NEEDS_WRAP;
    }
}



#define PRINT_RUN_MAX 256

static int term_putc_run(Term *t, const Event *ev, int start, int nev)
{
    int cols = screen_cols(t->screen);
    int x = t->cx;
    int cy = t->cy;
    int fast = (t->mode & MODE_WRAP) && !(t->mode & MODE_INSERT)
	&& t->charset_table[t->cur_charset] != CS_DEC_GRAPHICS
	&& !(t->cursor_state & (CURSOR_WRAPNEXT | CURSOR_INPUT_NEEDS_WRAP))
	&& x >= 0 && x < cols && cy >= 0;

    if (fast) {
	Cell cells[PRINT_RUN_MAX];
	Cell base = t->curcell;
	int m = 0;
	int i = start;

	while (i < nev && m < PRINT_RUN_MAX && x < cols
	       && ev[i].type == EVENT_PRINT) {
	    Rune r = ev[i].data.r;
	    if (r < 0x20 || r > 0x7E)
		break;
	    base.r = r;
	    cells[m++] = base;
	    x++;
	    i++;
	}

	if (m > 0) {
	    screen_put_cells(t->screen, t->cx, cy, cells, m);
	    t->last_rune = cells[m - 1].r;
	    if (x >= cols) {
		t->cx = cols - 1;
		t->cursor_state |= CURSOR_WRAPNEXT;
	    } else {
		t->cx = x;
	    }
	    return i - start;
	}
    }

    term_putc(t, ev[start].data.r);
    return 1;
}

static void term_def_charset(Term *t, uint8_t ascii)
{

    static const char cs_charset[] = { CS_DEC_GRAPHICS, CS_USA };
    int idx = (ascii == '0') ? 0 : 1;
    t->charset_table[t->sel_charset] = cs_charset[idx];
}



static void term_dec_test(Term *t, uint8_t c)
{
    if (c == '8') {

	int cols = screen_cols(t->screen);
	int rows = screen_rows(t->screen);
	Cell filler = t->curcell;
	filler.r = 'E';
	screen_clear_region(t->screen, 0, 0, cols - 1, rows - 1, filler);
    }
}



static void csi_handle(Term *t, const Event *ev)
{
    const CsiEvent *csi = &ev->data.csi;
    int lcl_arg[PARSER_CSI_ARGS];
    int narg = csi->narg;
    memcpy(lcl_arg, csi->arg, sizeof(lcl_arg));
    int *arg = lcl_arg;
    char final = (csi->nfinal > 0) ? csi->final[csi->nfinal - 1] : 0;
    char priv = csi->priv;


    if (csi->nfinal > 1) {
	for (size_t i = 0; i < (size_t) (csi->nfinal - 1); i++) {
	    if (csi->final[i] != ' ')
		goto unknown;
	}
    }
    char buf[40];
    int len;


    switch (final) {
    default:
	goto unknown;
    case '@':
	DEFAULT(arg[0], 1);
	term_insert_blank(t, arg[0]);
	break;
    case 'A':
	DEFAULT(arg[0], 1);
	term_move_to(t, t->cx, t->cy - arg[0]);
	break;
    case 'B':
    case 'e':
	DEFAULT(arg[0], 1);
	term_move_to(t, t->cx, t->cy + arg[0]);
	break;
    case 'i':
	switch (arg[0]) {
	case 0:
	case 1:
	case 2:
	    break;
	case 4:
	    t->mode &= ~MODE_PRINT;
	    break;
	case 5:
	    t->mode |= MODE_PRINT;
	    break;
	}
	break;
    case 'c':
	if (arg[0] == 0)
	    term_write(t, "\033[?6c", 5);
	break;
    case 'b':
	LIMIT(arg[0], 1, 65535);
	if (t->last_rune)
	    while (arg[0]-- > 0)
		term_putc(t, t->last_rune);
	break;
    case 'C':
    case 'a':
	DEFAULT(arg[0], 1);
	term_move_to(t, t->cx + arg[0], t->cy);
	break;
    case 'D':
	DEFAULT(arg[0], 1);
	term_move_to(t, t->cx - arg[0], t->cy);
	break;
    case 'E':
	DEFAULT(arg[0], 1);
	term_move_to(t, 0, t->cy + arg[0]);
	break;
    case 'F':
	DEFAULT(arg[0], 1);
	term_move_to(t, 0, t->cy - arg[0]);
	break;
    case 'g':
	switch (arg[0]) {
	case 0:
	    t->tabstops[t->cx] = 0;
	    break;
	case 3:
	    memset(t->tabstops, 0, t->ntabstops * sizeof(int));
	    break;
	default:
	    goto unknown;
	}
	break;
    case 'G':
    case '`':
	DEFAULT(arg[0], 1);
	term_move_to(t, arg[0] - 1, t->cy);
	break;
    case 'H':
    case 'f':
	DEFAULT(arg[0], 1);
	DEFAULT(arg[1], 1);
	term_move_abs(t, arg[1] - 1, arg[0] - 1);
	break;
    case 'I':
	DEFAULT(arg[0], 1);
	term_put_tab(t, arg[0]);
	break;
    case 'J':
	switch (arg[0]) {
	case 0:
	    term_clear_region(t, t->cx, t->cy, screen_cols(t->screen) - 1,
			      t->cy);
	    if (t->cy < screen_rows(t->screen) - 1)
		term_clear_region(t, 0, t->cy + 1,
				  screen_cols(t->screen) - 1,
				  screen_rows(t->screen) - 1);
	    break;
	case 1:
	    if (t->cy > 0)
		term_clear_region(t, 0, 0, screen_cols(t->screen) - 1,
				  t->cy - 1);
	    term_clear_region(t, 0, t->cy, t->cx, t->cy);
	    break;
	case 2:
	    term_clear_region(t, 0, 0,
			      screen_cols(t->screen) - 1,
			      screen_rows(t->screen) - 1);
	    break;
	default:
	    goto unknown;
	}
	break;
    case 'K':
	switch (arg[0]) {
	case 0:
	    term_clear_region(t, t->cx, t->cy, screen_cols(t->screen) - 1,
			      t->cy);
	    break;
	case 1:
	    term_clear_region(t, 0, t->cy, t->cx, t->cy);
	    break;
	case 2:
	    term_clear_region(t, 0, t->cy, screen_cols(t->screen) - 1,
			      t->cy);
	    break;
	}
	break;
    case 'S':
	if (priv)
	    break;
	DEFAULT(arg[0], 1);
	term_scroll_up(t, t->top, t->bot, arg[0]);
	break;
    case 'T':
	DEFAULT(arg[0], 1);
	term_scroll_down(t, t->top, t->bot, arg[0]);
	break;
    case 'L':
	DEFAULT(arg[0], 1);
	screen_insert_lines(t->screen, t->cy, t->top, t->bot, arg[0]);
	break;
    case 'l':
	csi_set_mode(t, priv, 0, arg, narg);
	break;
    case 'M':
	DEFAULT(arg[0], 1);
	screen_delete_lines(t->screen, t->cy, t->top, t->bot, arg[0]);
	break;
    case 'X':
	DEFAULT(arg[0], 1);
	term_clear_region(t, t->cx, t->cy, t->cx + arg[0] - 1, t->cy);
	break;
    case 'P':
	DEFAULT(arg[0], 1);
	screen_delete_char(t->screen, t->cx, t->cy, arg[0]);
	break;
    case 'Z':
	DEFAULT(arg[0], 1);
	term_put_tab(t, -arg[0]);
	break;
    case 'd':
	DEFAULT(arg[0], 1);
	term_move_abs(t, t->cx, arg[0] - 1);
	break;
    case 'q':
	if (csi->nfinal >= 2 && csi->final[0] == ' ') {
	    int shape = arg[0];
	    if (shape == CURSOR_SHAPE_DEFAULT)
		shape = CURSOR_SHAPE_BLOCK;
	    if (BETWEEN(shape, CURSOR_SHAPE_DEFAULT, CURSOR_SHAPE_BAR))
		t->cursor_shape = shape;
	    else
		goto unknown;
	} else if (priv && arg[0] == 0) {
	    break;
	} else {
	    goto unknown;
	}
	break;
    case 'h':
	csi_set_mode(t, priv, 1, arg, narg);
	break;
    case 'm':
	term_set_attr(t, arg, narg);
	break;
    case 'n':
	switch (arg[0]) {
	case 5:
	    term_write(t, "\033[0n", 4);
	    break;
	case 6:
	    len = snprintf(buf, sizeof(buf), "\033[%i;%iR",
			   t->cy + 1, t->cx + 1);
	    term_write(t, buf, len);
	    break;
	case 996:
	    break;
	default:
	    goto unknown;
	}
	break;
    case 'r':
	if (priv)
	    goto unknown;
	DEFAULT(arg[0], 1);
	DEFAULT(arg[1], screen_rows(t->screen));
	t->top = arg[0] - 1;
	t->bot = arg[1] - 1;
	LIMIT(t->top, 0, screen_rows(t->screen) - 1);
	LIMIT(t->bot, 0, screen_rows(t->screen) - 1);
	if (t->top > t->bot) {
	    int tmp = t->top;
	    t->top = t->bot;
	    t->bot = tmp;
	}
	term_move_abs(t, 0, 0);
	break;
    case 's':
	if (priv)
	    goto unknown;
	term_cursor(t, CURSOR_SAVE);
	break;
    case 'u':
	if (priv)
	    goto unknown;
	term_cursor(t, CURSOR_LOAD);
	break;
    case 't':
	if (priv) {
	    switch (arg[0]) {
	    case 1:
		break;
	    case 2:
		break;
	    case 3:
		break;
	    case 4:
	    case 5:
	    case 10:
	    case 11:
	    case 13:
	    case 14:
	    case 16:
	    case 19:
	    case 20:
	    case 21:
	    case 24:
	    case 25:
	    case 26:
	    case 28:
	    case 29:
	    case 30:
	    case 31:
	    case 32:
	    case 33:
	    case 34:
	    case 39:
	    case 40:
	    case 41:
	    case 42:
	    case 43:
	    case 44:
	    case 45:
	    case 46:
	    case 47:
	    case 99:
		break;
	    default:
		goto unknown;
	    }
	} else if (arg[0] == 18) {
	    char buf[32];
	    int n = snprintf(buf, sizeof(buf), "\033[8;%zu;%zu",
			     screen_rows(t->screen),
			     screen_cols(t->screen));
	    term_write(t, buf, (size_t) n);
	}
	break;
    }
    return;
  unknown:
    fprintf(stderr, "erresc: unknown csi ");
    if (priv)
	fprintf(stderr, "?");
    for (int i = 0; i < narg; i++)
	fprintf(stderr, "%d%c", arg[i], i + 1 < narg ? ';' : ' ');
    fprintf(stderr, "'%c'\n", final);

}



static char *base64dec(const char *src)
{
    size_t in_len = strlen(src);
    char *result, *dst;
    const char *end = src + in_len;
    static const signed char base64_digits[256] = {
	[43] = 62, 0, 0, 0, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
	0, 0, 0, -1, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
	13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0, 0, 0, 0,
	0, 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
	40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
    };

    if (in_len % 4)
	in_len += 4 - (in_len % 4);
    result = dst = malloc(in_len / 4 * 3 + 1);
    if (!result)
	return NULL;
    while (src < end) {
	int a = base64_digits[(unsigned char) *src];
	if (a < 0) {
	    src++;
	    continue;
	}
	if (src + 1 >= end)
	    break;
	int b = base64_digits[(unsigned char) *(++src)];
	if (b < 0)
	    b = 0;
	if (src + 1 >= end)
	    break;
	int c = base64_digits[(unsigned char) *(++src)];
	if (c < 0)
	    c = 0;
	if (src + 1 >= end)
	    break;
	int d = base64_digits[(unsigned char) *(++src)];
	if (d < 0)
	    d = 0;
	*dst++ = (a << 2) | ((b & 0x30) >> 4);
	if (c > 0)
	    *dst++ = ((b & 0x0f) << 4) | ((c & 0x3c) >> 2);
	if (d > 0)
	    *dst++ = ((c & 0x03) << 6) | d;
	src++;
    }
    *dst = '\0';
    return result;
}



static void osc_colour_response(Term *t, int par, int idx, int is_osc4)
{
    Argb c = palette_get(t->pal, idx);
    unsigned char r = (c >> 16) & 0xFF;
    unsigned char g = (c >> 8) & 0xFF;
    unsigned char b = (c >> 0) & 0xFF;
    char buf[64];
    int n;
    if (is_osc4)
	n = snprintf(buf, sizeof(buf),
		     "\033]4;%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", par,
		     r, r, g, g, b, b);
    else
	n = snprintf(buf, sizeof(buf),
		     "\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", par, r,
		     r, g, g, b, b);
    if (n > 0 && n < (int) sizeof(buf))
	term_write(t, buf, n);
}



static const struct {
    int idx;
    const char *str;
} osc_table[] = {
    { PAL_DEFAULT_FG, "foreground" },
    { PAL_DEFAULT_BG, "background" },
    { PAL_DEFAULT_CS, "cursor" },
};

static int osc_split(char *buf, char **args, int maxargs)
{
    int na = 0;
    char *s = buf;

    while (na < maxargs) {
	args[na] = s;
	char *sep = strchr(s, ';');
	if (!sep) {
	    na++;
	    break;
	}
	*sep = '\0';
	s = sep + 1;
	na++;
    }
    return na;
}

static void str_handle(Term *t, const Event *ev)
{
    const StringEvent *str = &ev->data.str;
    char *args[PARSER_STR_ARGS];
    int narg = 0;
    const char *p = NULL;
    int j;

    if (ev->type == EVENT_OSC && str->buf)
	narg = osc_split(str->buf, args, PARSER_STR_ARGS);

    switch (ev->type) {
    case EVENT_OSC:;

	if (ev->data.str.par == 'k') {
	    if (narg > 0) {
		snprintf(t->title, sizeof(t->title), "%s", args[0]);
		if (t->title_fn)
		    t->title_fn(t->title, t->title_ctx);
	    }
	    return;
	}

	if (narg < 1)
	    break;
	const char *osc_type = args[0];
	int par = atoi(osc_type);
	switch (par) {
	case 0:
	    if (narg > 1) {
		snprintf(t->title, sizeof(t->title), "%s", args[1]);
		if (t->title_fn)
		    t->title_fn(t->title, t->title_ctx);
	    }
	    return;
	case 1:
	    return;
	case 2:
	    if (narg > 1) {
		snprintf(t->title, sizeof(t->title), "%s", args[1]);
		if (t->title_fn)
		    t->title_fn(t->title, t->title_ctx);
	    }
	    return;
	case 7:
	case 133:
	    return;
	case 52:
	    if (narg >= 3 && args[2]) {
		if (!strcmp(args[2], "?")) {
		    if (t->clip[0]) {
			int clip_len = (int) strlen(t->clip);
			char resp[4096 + 64];
			static const char b64[] =
			    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			    "abcdefghijklmnopqrstuvwxyz"
			    "0123456789+/";
			int n = snprintf(resp, sizeof(resp),
					 "\033]52;%s;", args[1]);
			int i;
			for (i = 0; i + 3 <= clip_len; i += 3) {
			    uint8_t b0 = (uint8_t) t->clip[i];
			    uint8_t b1 = (uint8_t) t->clip[i + 1];
			    uint8_t b2 = (uint8_t) t->clip[i + 2];
			    resp[n++] = b64[b0 >> 2];
			    resp[n++] = b64[((b0 & 3) << 4) | (b1 >> 4)];
			    resp[n++] = b64[((b1 & 0x0F) << 2) | (b2 >> 6)];
			    resp[n++] = b64[b2 & 0x3F];
			}
			if (i + 1 == clip_len) {
			    uint8_t b0 = (uint8_t) t->clip[i];
			    resp[n++] = b64[b0 >> 2];
			    resp[n++] = b64[(b0 & 3) << 4];
			    resp[n++] = '=';
			    resp[n++] = '=';
			} else if (i + 2 == clip_len) {
			    uint8_t b0 = (uint8_t) t->clip[i];
			    uint8_t b1 = (uint8_t) t->clip[i + 1];
			    resp[n++] = b64[b0 >> 2];
			    resp[n++] = b64[((b0 & 3) << 4) | (b1 >> 4)];
			    resp[n++] = b64[(b1 & 0x0F) << 2];
			    resp[n++] = '=';
			}
			resp[n++] = '\033';
			resp[n++] = '\\';
			term_write(t, resp, (size_t) n);
		    }
		} else {
		    char *decoded = base64dec(args[2]);
		    if (decoded) {
			int dlen = (int) strlen(decoded);
			if (dlen < (int) sizeof(t->clip) - 1)
			    memcpy(t->clip, decoded, (size_t) dlen + 1);
			free(decoded);
		    }
		}
	    }
	    return;
	case 10:
	case 11:
	case 12:
	    if (narg < 2)
		break;
	    p = args[1];
	    j = par - 10;
	    if (j < 0 || j >= (int) LEN(osc_table))
		break;
	    if (!strcmp(p, "?")) {
		osc_colour_response(t, par, osc_table[j].idx, 0);
	    } else {
		Argb c;
		if (colour_parse(p, &c))
		    palette_set(t->pal, osc_table[j].idx, c);
		else
		    fprintf(stderr,
			    "erresc: invalid %s colour: %s\n",
			    osc_table[j].str, p);
	    }
	    return;
	case 4:
	    if (narg < 3)
		break;
	    j = atoi(args[1]);
	    p = args[2];
	    if (!strcmp(p, "?")) {
		osc_colour_response(t, j, j, 1);
	    } else {
		Argb c;
		if (colour_parse(p, &c))
		    palette_set(t->pal, j, c);
		else
		    fprintf(stderr,
			    "erresc: invalid colour for index %d: %s\n", j,
			    p);
	    }
	    return;

	case 104:
	    j = (narg > 1) ? atoi(args[1]) : -1;
	    p = (narg > 2) ? args[2] : NULL;
	    if (p && !strcmp(p, "?")) {
		osc_colour_response(t, j, j, 1);
	    } else if (p) {
		Argb c;
		if (colour_parse(p, &c))
		    palette_set(t->pal, j, c);
		else
		    fprintf(stderr,
			    "erresc: invalid colour j=%d, p=%s\n",
			    j, p ? p : "(null)");
	    } else {
		if (par == 104 && narg <= 1) {

		    palette_reload(t->pal);
		    return;
		}
		if (j >= 0)
		    palette_reset(t->pal, j);
	    }
	    return;
	case 110:
	case 111:
	case 112:
	    if (narg != 1)
		break;
	    j = par - 110;
	    if (j < 0 || j >= (int) LEN(osc_table))
		break;
	    palette_reset(t->pal, osc_table[j].idx);
	    return;
	}
	break;
    case EVENT_DCS:
    case EVENT_APC:
    case EVENT_PM:
	return;
    default:
	break;
    }

    fprintf(stderr, "erresc: unknown str ");
    for (int i = 0; i < narg; i++)
	fprintf(stderr, " %s", args[i]);
    fprintf(stderr, "\n");
}



static void esc_handle(Term *t, uint8_t prefix, uint8_t b)
{

    if (prefix) {
	switch (prefix) {
	case '#':
	    switch (b) {
	    case '8':
		term_dec_test(t, b);
		break;
	    default:
		goto unknown_esc;
	    }
	    return;
	case '(':
	case ')':
	case '*':
	case '+':
	    t->sel_charset = prefix - '(';
	    term_def_charset(t, b);
	    return;
	case '%':
	    if (b == 'G' || b == '@')
		return;
	    goto unknown_esc;
	case ' ':
	    if (b == 'F' || b == 'G')
		return;
	    goto unknown_esc;
	}
	goto unknown_esc;
    }


    switch (b) {
    case 'D':
	if (t->cy == t->bot)
	    term_scroll_up(t, t->top, t->bot, 1);
	else
	    term_move_to(t, t->cx, t->cy + 1);
	break;
    case 'E':
	term_newline(t, 1);
	break;
    case 'H':
	t->tabstops[t->cx] = 1;
	break;
    case 'M':
	if (t->cy == t->top)
	    term_scroll_down(t, t->top, t->bot, 1);
	else
	    term_move_to(t, t->cx, t->cy - 1);
	break;
    case 'Z':
	term_write(t, "\033[?6c", 5);
	break;
    case 'c':
	term_reset(t);
	break;
    case '=':
	t->mode |= MODE_APPKEYPAD;
	break;
    case '>':
	t->mode &= ~MODE_APPKEYPAD;
	break;
    case '7':
	term_cursor(t, CURSOR_SAVE);
	break;
    case '8':
	term_cursor(t, CURSOR_LOAD);
	break;
    case 'n':
	t->cur_charset = 2;
	break;
    case 'o':
	t->cur_charset = 3;
	break;
    case 'k':
	break;
    case '\\':
	break;
    default:
	goto unknown_esc;
    }
    return;
  unknown_esc:
    fprintf(stderr,
	    "erresc: unknown sequence ESC 0x%02X '%c'\n",
	    b, (b >= 32 && b < 127) ? b : '.');
}



static void term_control_code(Term *t, uint8_t ascii)
{
    switch (ascii) {
    case '\t':
	term_put_tab(t, 1);
	return;
    case '\b':
	term_move_to(t, t->cx - 1, t->cy);
	return;
    case '\r':
	term_move_to(t, 0, t->cy);
	return;
    case '\f':
    case '\v':
    case '\n':
	term_newline(t, t->mode & MODE_CRLF);
	return;
    case '\a':
	if (t->mode & MODE_PRINT)
	    break;
	term_bell(t);
	return;
    case '\033':
	return;
    case '\016':
    case '\017':
	t->cur_charset = 1 - (ascii - '\016');
	return;
    case '\032':
	{
	    Cell c = t->curcell;
	    c.r = '?';
	    screen_set(t->screen, t->cx, t->cy, c);
	    break;
	}
    case '\030':
	t->last_rune = 0;
	return;
    case '\005':
    case '\000':
    case '\021':
    case '\023':
    case 0177:
	return;
    default:
	return;
    }
}



static void control_dispatch(Term *t, const Event *ev)
{
    switch (ev->type) {
    case EVENT_CONTROL:
	term_control_code(t, ev->data.code);
	break;
    case EVENT_PRINT:
	term_putc(t, ev->data.r);
	break;
    case EVENT_CSI:
	csi_handle(t, ev);
	break;
    case EVENT_OSC:
	str_handle(t, ev);
	break;
    case EVENT_ESC:
	esc_handle(t, ev->data.esc.prefix, ev->data.esc.b);
	break;
    case EVENT_DCS:
    case EVENT_APC:
    case EVENT_PM:
	break;
    default:
	break;
    }
}

void control_dispatch_batch(Term *t, const Event *ev, int nev)
{
    int i = 0;

    while (i < nev) {
	if (ev[i].type == EVENT_PRINT)
	    i += term_putc_run(t, ev, i, nev);
	else {
	    control_dispatch(t, &ev[i]);
	    i++;
	}
    }
}
