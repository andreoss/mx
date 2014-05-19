#include <errno.h>
#include <fontconfig/fontconfig.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon.h>

#include "config.h"
#include "frontend.h"
#include "libccor/ccor.h"
#include "parser.h"
#include "proc.h"
#include "pty.h"
#include "screen.h"
#include "term.h"
#include "types.h"
#include "utf8.h"


typedef enum {
    MOUSE_PRESS,
    MOUSE_RELEASE,
    MOUSE_MOTION,
} MouseAction;

#define MOUSE_BUTTONS    11
#define MOUSE_NO_BUTTON  12
#define MOUSE_OFFSET     32
#define MOUSE_COORD_MAX  223
#define MOUSE_RELEASE_B  3
#define MOUSE_MID_B      64
#define MOUSE_HIGH_B     128
#define MOUSE_MID_BTN   4
#define MOUSE_HIGH_BTN  8
#define MOUSE_MOD_SHIFT  4
#define MOUSE_MOD_ALT    8
#define MOUSE_MOD_CTRL   16

#define BUF_TITLE 256
#define BUF_EVENT 1024
#define BUF_PTY   8196

#define CTRL_MASK 0x1F
#define KEY_DEL   0x7F
#define BYTE_MAX  0xFF

typedef struct Input Input;


static void palette_random_generate(void);
static void palette_flip_dark_light(void);



static Term *term;
static Parser *parser;
static FrontendProto renderer;
static FrontendProto frontend_proto;
static int mod_switch_mask;
static Palette *palette;
static ColourCorrection current_cc;
static Pty *pty;
static Input *input;

static xcb_connection_t *conn;
static xcb_key_symbols_t *keysyms;
static xcb_screen_t *xcb_screen;
static xcb_window_t xcb_win;
static xcb_atom_t xcb_net_wm_name;
static xcb_atom_t xcb_utf8_string;


static volatile sig_atomic_t sigchld_received;
static volatile int running = 1;


static char base_title[BUF_TITLE];
static char proc_prefix[BUF_TITLE];
static struct timespec last_proccheck;


static char *sel_buf;
static size_t sel_len;
static xcb_atom_t xcb_sel_prop;
static xcb_atom_t xcb_sel_targets;
static xcb_atom_t xcb_clipboard;
static xcb_timestamp_t xcb_last_time;


enum {
    FLAG_SEL_PENDING = 1 << 0,
    FLAG_ON_RESIZE_RESIZE = 1 << 1,
    FLAG_OPT_KEEP = 1 << 2,
    FLAG_CHILD_EXITED = 1 << 3,
    FLAG_DRAWING = 1 << 4,
    FLAG_CTRL_S_PREFIX = 1 << 5,
    FLAG_BLINK_HAS_SLOW = 1 << 6,
    FLAG_WIN_FOCUSED = 1 << 7,
    FLAG_CONF_PENDING = 1 << 8,
};
static unsigned flags = FLAG_ON_RESIZE_RESIZE | FLAG_WIN_FOCUSED;


static double minlatency_val = MIN_LATENCY_MS;
static double maxlatency_val = MAX_LATENCY_MS;
static unsigned int blinktimeout_val = BLINK_TIMEOUT_MS;
static unsigned int blinktimeout_fast_val = BLINK_FAST_TIMEOUT_MS;
static struct timespec lastblink;
static struct timespec draw_trigger;
static struct timespec last_frame_time;

static struct timespec ctrl_s_time;
static double current_font_scale = FONT_SCALE_INIT;
static unsigned int cursor_blinktimeout_val = CURSOR_BLINK_TIMEOUT_MS;
static struct timespec last_cursor_blink;
static double cursor_alpha_sent = 1.0;
static double cursor_tick_delay = CURSOR_TICK_DELAY_MS;

static int cursor_shape_current(void)
{
    return term ? term_cursor_shape(term) : 0;
}

static void title_cb(const char *title, void *user);

static void zoom(double delta)
{
    current_font_scale += delta;
    if (current_font_scale < 0.1)
	current_font_scale = 0.1;
    if (current_font_scale > 10.0)
	current_font_scale = 10.0;
    frontend_set_font_scale(&renderer, current_font_scale);
    int winw = frontend_actual_width(&renderer);
    int winh = frontend_actual_height(&renderer);
    if (winw <= 0 || winh <= 0) {
	winw = frontend_expected_width(&renderer);
	winh = frontend_expected_height(&renderer);
    }
    int border = frontend_border(&renderer);
    int cw = frontend_char_width(&renderer);
    int ch = frontend_char_height(&renderer);
    int cols = (winw - 2 * border) / cw;
    int rows = (winh - 2 * border) / ch;
    if (cols < 1)
	cols = 1;
    if (rows < 1)
	rows = 1;
    frontend_resize(&renderer, cols, rows);
    term_resize(term, cols, rows);
    pty_resize(pty, cols, rows);
    pty_signal(pty, SIGWINCH);
    term_dirty(term);
    clock_gettime(CLOCK_MONOTONIC, &draw_trigger);
    flags |= FLAG_DRAWING;
}

static void die(const char *msg)
{
    fprintf(stderr, "term: %s\n", msg);
    exit(1);
}



static void sigchld_handler(int n)
{
    UNUSED(n);
    sigchld_received = 1;
}


static void sigterm_handler(int n)
{
    UNUSED(n);
    running = 0;
}



static void write_cb(const char *buf, size_t len, void *user)
{
    UNUSED(user);
    pty_write(pty, buf, len);
}



static void paste_request(xcb_atom_t atom)
{
    if (!conn || (flags & FLAG_SEL_PENDING))
	return;
    flags |= FLAG_SEL_PENDING;
    xcb_convert_selection(conn, xcb_win, atom, xcb_utf8_string
			  ? xcb_utf8_string : XCB_ATOM_STRING,
			  xcb_sel_prop, xcb_last_time);
    xcb_flush(conn);
}

static void bell_cb(void *user)
{
    UNUSED(user);
    frontend_bell(&renderer);
}

static void set_window_title(const char *title)
{
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, xcb_win,
			XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
			strlen(title), title);
    if (xcb_net_wm_name)
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, xcb_win,
			    xcb_net_wm_name, xcb_utf8_string, 8,
			    strlen(title), title);
    xcb_flush(conn);
}

static void update_title(void)
{
    char full[sizeof base_title + sizeof proc_prefix + 4];

    if (proc_prefix[0])
	snprintf(full, sizeof full, "%s : %s", proc_prefix, base_title);
    else
	snprintf(full, sizeof full, "%s", base_title);
    set_window_title(full);
}

static void refresh_proc_prefix(void)
{
    char np[BUF_TITLE];

    if (!pty)
	return;
    if (!proc_chain(pty, np, sizeof np))
	return;
    if (strcmp(np, proc_prefix) != 0) {
	snprintf(proc_prefix, sizeof proc_prefix, "%s", np);
	update_title();
    }
}

static void title_cb(const char *title, void *user)
{
    UNUSED(user);
    if (title)
	snprintf(base_title, sizeof base_title, "%s", title);
    update_title();
}



static void update_back_pixel(void)
{
    static uint32_t last = 0xFFFFFFFF;
    if (!conn || !xcb_win || !palette)
	return;
    Argb bg = palette_resolve_corrected(palette, PAL_DEFAULT_BG,
					&current_cc);
    uint32_t px = bg & 0x00FFFFFF;
    if (px == last)
	return;
    last = px;
    xcb_change_window_attributes(conn, xcb_win, XCB_CW_BACK_PIXEL, &px);
}

static void palette_random_generate(void)
{
    palette_randomise(palette);
    current_cc = colour_regression_ccm(palette);
    frontend_set_colour_correction(&renderer, &current_cc);
    update_back_pixel();
    term_dirty(term);
    flags |= FLAG_DRAWING;
}

static void palette_flip_dark_light(void)
{
    palette_flip_fg_bg(palette);
    current_cc = colour_regression_ccm(palette);
    frontend_set_colour_correction(&renderer, &current_cc);
    update_back_pixel();
    term_dirty(term);
    flags |= FLAG_DRAWING;
}

static int term_has_blink(const Term *t)
{
    return screen_has_blink(term_screen(t));
}



static char utf8_leftover[4];
static size_t utf8_leftover_len = 0;

static void ttyread_handler(void)
{
    char buf[BUF_PTY];
    Event ev[BUF_EVENT];
    int nev;

    size_t left = utf8_leftover_len;
    memcpy(buf, utf8_leftover, left);
    int n = 0, r = -2;
    while ((size_t) n < sizeof(buf) - 1 - left) {
	r = pty_read(pty, buf + left + n,
		     (int) (sizeof(buf) - 1 - left - (size_t) n));
	if (r <= 0)
	    break;
	n += r;
    }
    utf8_leftover_len = 0;

    if (r == -2 && n == 0)
	return;
    if (n > 0) {
	n += (int) left;


	size_t off = 0;
	while (off < (size_t) n) {
	    size_t chunk = n - off;
	    if (chunk > BUF_EVENT)
		chunk = BUF_EVENT;
	    if (off + chunk < (size_t) n)
		while (chunk > 1
		       && ((unsigned char) buf[off + chunk] & 0xC0) ==
		       0x80)
		    chunk--;
	    else {
		int save = 0;
		for (int b = 0; b < 4 && b < (int) chunk; b++) {
		    unsigned char c =
			(unsigned char) buf[off + chunk - 1 - b];
		    if ((c & 0x80) == 0)
			break;
		    if (c >= 0xC0 && c <= 0xF4) {
			int need = 1;
			if ((c & 0xE0) == 0xC0)
			    need = 2;
			else if ((c & 0xF0) == 0xE0)
			    need = 3;
			else if ((c & 0xF8) == 0xF0)
			    need = 4;
			if (b + 1 < need)
			    save = b + 1;
			break;
		    }
		}
		if (save > 0) {
		    memcpy(utf8_leftover, buf + off + chunk - save,
			   (size_t) save);
		    utf8_leftover_len = (size_t) save;
		    chunk -= (size_t) save;
		}
	    }
	    nev = parser_feed(parser, buf + off, chunk, ev, LEN(ev));
	    term_process_batch(term, ev, nev);
	    for (int i = 0; i < nev; i++) {
		if (ev[i].type == EVENT_OSC || ev[i].type == EVENT_DCS ||
		    ev[i].type == EVENT_APC || ev[i].type == EVENT_PM ||
		    ev[i].type == EVENT_SOS)
		    free(ev[i].data.str.buf);
	    }
	    off += chunk;
	    if (utf8_leftover_len > 0)
		break;
	}
    } else {

	if (flags & FLAG_OPT_KEEP) {
	    pty_close(pty);
	    flags |= FLAG_CHILD_EXITED;
	} else {
	    running = 0;
	}
    }
}




static void send_key(uint32_t ksym, uint modmask)
{
    if (ksym == XK_Insert && (modmask & 1)) {
	paste_request(XCB_ATOM_PRIMARY);
	return;
    }
    if (term)
	term_set_mode(term, 0, MODE_HIDE);
    if (pty) {
	for (size_t i = 0; i < LEN(key); i++) {
	    if (key[i].keysym != ksym)
		continue;
	    if (key[i].mask != XK_ANY_MOD
		&& key[i].mask != (modmask
				   & ~(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2 |
				       XCB_MOD_MASK_3)))
		continue;
	    int mode = term_mode_raw(term);
	    int appkeypad = mode & MODE_APPKEYPAD;
	    int appcursor = mode & MODE_APPCURSOR;
	    if ((appkeypad ? key[i].appkey < 0 : key[i].appkey > 0))
		continue;
	    if ((appcursor ? key[i].appcursor < 0 : key[i].appcursor > 0))
		continue;
	    if (key[i].s) {
		pty_write(pty, key[i].s, strlen(key[i].s));
		return;
	    }
	}
	int ctrl = (modmask & XCB_MOD_MASK_CONTROL) != 0;
	int alt = (modmask & XCB_MOD_MASK_1) != 0;
	uint32_t ch = ksym;
	char buf[8];
	size_t len = 0;
	if (ctrl) {
	    if (ch >= 'A' && ch <= 'Z')
		ch -= 'A' - 1;
	    else if (ch >= 'a' && ch <= 'z')
		ch -= 'a' - 1;
	    else if (ch >= '[' && ch <= '_')
		ch -= 'A' - 1;
	    else if (ch == ' ' || ch == '2')
		ch = 0;
	    else if (ch == '/')
		ch = CTRL_MASK;
	    else if (ch == '?')
		ch = KEY_DEL;
	    else if (ch > BYTE_MAX)
		ch &= CTRL_MASK;
	}
	if (alt)
	    pty_write(pty, "\033", 1);
	if (ch <= BYTE_MAX) {
	    buf[0] = (char) ch;
	    len = 1;
	} else {
	    uint32_t u = xkb_keysym_to_utf32(ch);
	    if (u == 0)
		return;
	    len = utf8_encode(u, buf);
	}
	if (len > 0)
	    pty_write(pty, buf, len);
    }
}


static void event_coords(int ex, int ey, int *col, int *row)
{
    *col =
	(ex - frontend_border(&renderer)) / frontend_char_width(&renderer);
    *row =
	(ey -
	 frontend_border(&renderer)) / frontend_char_height(&renderer);
}


struct Input {
    int buttons;
};

static Input *input_new(void)
{
    return calloc(1, sizeof(struct Input));
}

static void input_free(Input *t)
{
    free(t);
}

static void input_sel_start(Term *term, int col, int row)
{
    term_sel_clear(term);
    term_sel_start(term, col, row, SNAP_NONE);
    term_dirty(term);
    clock_gettime(CLOCK_MONOTONIC, &draw_trigger);
    flags |= FLAG_DRAWING;
}

static void input_sel_extend(Term *term, int col, int row)
{
    if (!term_sel_active(term))
	return;
    term_sel_extend(term, col, row);
    term_dirty(term);
    clock_gettime(CLOCK_MONOTONIC, &draw_trigger);
    flags |= FLAG_DRAWING;
}

static int input_sel_active(const Input *t)
{
    UNUSED(t);
    return term_sel_active(NULL);
}

static void input_mouse(Input *t, int button, unsigned int modmask,
			int col, int row, MouseAction type, void *pty,
			Term *term)
{
    char buf[40];
    int len, btn = button;
    int code = 0;
    int x = col + 1, y = row + 1;
    int sgr = term_mode(term, MODE_MOUSESGR);
    int motion = term_mode(term, MODE_MOUSEMOTION);
    int many = term_mode(term, MODE_MOUSEMANY);

    if (type == MOUSE_MOTION) {
	if (t->buttons & 1) {
	    input_sel_extend(term, col, row);
	    return;
	}
	if (!motion && !many)
	    return;
	if (motion && t->buttons == 0)
	    return;
	for (btn = 1; btn <= MOUSE_BUTTONS && !(t->buttons & (1 << (btn - 1)));
	     btn++);
	code = MOUSE_OFFSET;
    } else {
	if (button < 1 || button > MOUSE_BUTTONS)
	    return;
	if (type == MOUSE_RELEASE) {
	    if (button == 4 || button == 5)
		return;
	    if (button == 1 && term_sel_active(term)) {
		t->buttons &= ~(1 << (button - 1));
		return;
	    }
	}
	if (type == MOUSE_PRESS) {
	    if (button == 1 && (!sgr || (modmask & XCB_MOD_MASK_SHIFT))) {
		t->buttons |= 1;
		input_sel_start(term, col, row);
		return;
	    }
	}
	code = 0;
    }

    if (!term_mode(term, MODE_MOUSE))
	return;

    if ((!sgr && type == MOUSE_RELEASE) || btn == MOUSE_NO_BUTTON)
	code += MOUSE_RELEASE_B;
    else if (btn >= MOUSE_HIGH_BTN)
	code += MOUSE_HIGH_B + btn - MOUSE_HIGH_BTN;
    else if (btn >= MOUSE_MID_BTN)
	code += MOUSE_MID_B + btn - MOUSE_MID_BTN;
    else
	code += btn - 1;

    if (btn != 0 && btn != MOUSE_NO_BUTTON && !term_mode(term, MODE_MOUSEX10)) {
	code += (modmask & XCB_MOD_MASK_SHIFT ? MOUSE_MOD_SHIFT : 0)
	    + (modmask & XCB_MOD_MASK_1 ? MOUSE_MOD_ALT : 0)
	    + (modmask & XCB_MOD_MASK_CONTROL ? MOUSE_MOD_CTRL : 0);
    }

    if (sgr) {
	len = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
		       code, x, y, type == MOUSE_RELEASE ? 'm' : 'M');
    } else if (x < MOUSE_COORD_MAX && y < MOUSE_COORD_MAX) {
	len = snprintf(buf, sizeof(buf), "\033[M%c%c%c",
		       MOUSE_OFFSET + code, MOUSE_OFFSET + x, MOUSE_OFFSET + y);
    } else {
	return;
    }
    pty_write(pty, buf, len);
}

static int pending_conf_w;
static int pending_conf_h;

static void apply_configure(void)
{
    if (!(flags & FLAG_CONF_PENDING))
	return;
    flags &= ~FLAG_CONF_PENDING;
    int w = pending_conf_w;
    int h = pending_conf_h;
    if (w == frontend_actual_width(&renderer)
	&& h == frontend_actual_height(&renderer))
	return;
    int cols =
	(w - 2 * frontend_border(&renderer)) /
	frontend_char_width(&renderer);
    int rows =
	(h - 2 * frontend_border(&renderer)) /
	frontend_char_height(&renderer);
    if (cols > 0 && rows > 0 && (flags & FLAG_ON_RESIZE_RESIZE)
	&& (cols != (int) screen_cols(term_screen(term))
	    || rows != (int) screen_rows(term_screen(term)))) {
	term_resize(term, cols, rows);
	frontend_resize(&renderer, cols, rows);
	pty_resize(pty, cols, rows);
	pty_signal(pty, SIGWINCH);
    }
    frontend_resize_window(&renderer, w, h);
}

static void handle_xcb_event(xcb_generic_event_t *ge)
{
    uint8_t type = ge->response_type & ~0x80;

    switch (type) {
    case XCB_EXPOSE:
	frontend_damage(&renderer);
	term_dirty(term);
	break;

    case XCB_KEY_PRESS:
	{
	    xcb_key_press_event_t *kp = (xcb_key_press_event_t *) ge;
	    xcb_last_time = kp->time;
	    int col = 0;
	    if (kp->state & XCB_KEY_BUT_MASK_SHIFT)
		col |= 1;
	    if (mod_switch_mask && (kp->state & mod_switch_mask))
		col |= 2;
	    xcb_keysym_t ksym =
		xcb_key_symbols_get_keysym(keysyms, kp->detail, col);
	    if (ksym == XCB_NO_SYMBOL)
		ksym = xcb_key_symbols_get_keysym(keysyms, kp->detail,
						  col & ~1);
	    xcb_keysym_t base_ksym = ksym;
	    if (xcb_key_symbols_get_keysym(keysyms, kp->detail, 0) ==
		XK_space)
		ksym = XK_space;
	    if ((kp->state & XCB_MOD_MASK_CONTROL) && ksym > BYTE_MAX)
		base_ksym = xcb_key_symbols_get_keysym(keysyms, kp->detail,
						       col & 3);

	    if (flags & FLAG_CTRL_S_PREFIX) {
		if ((ksym >= XK_Shift_L && ksym <= XK_Hyper_R)
		    || ksym == XK_Mode_switch
		    || ksym == XK_ISO_Level3_Shift
		    || ksym == XK_ISO_Level5_Shift)
		    break;
		flags &= ~FLAG_CTRL_S_PREFIX;
		memset(&ctrl_s_time, 0, sizeof(ctrl_s_time));
		if (ksym == '+' || ksym == '=') {
		    zoom(0.1);
		    break;
		}
		if (ksym == '-') {
		    zoom(-0.1);
		    break;
		}
		if (!(kp->state & (XCB_KEY_BUT_MASK_SHIFT |
				   XCB_KEY_BUT_MASK_CONTROL |
				   XCB_KEY_BUT_MASK_MOD_1 |
				   XCB_KEY_BUT_MASK_MOD_4 |
				   XCB_KEY_BUT_MASK_MOD_5))) {
		    if (ksym == XK_c) {
			palette_random_generate();
			break;
		    }
		    if (ksym == XK_i) {
			palette_flip_dark_light();
			break;
		    }
		}
		if (ksym == 0x0073) {
		    clock_gettime(CLOCK_MONOTONIC, &ctrl_s_time);
		    pty_write(pty, "\x13", 1);
		    break;
		}
		if (ksym == XK_l) {
		    frontend_resize(&renderer,
				    (int) screen_cols(term_screen(term)),
				    (int) screen_rows(term_screen(term)));
		    term_dirty(term);
		    clock_gettime(CLOCK_MONOTONIC, &draw_trigger);
		    flags |= FLAG_DRAWING;
		    break;
		}
		send_key((uint32_t) ksym,
			 kp->state & ~XCB_KEY_BUT_MASK_CONTROL);
		break;
	    }

	    if ((kp->state & XCB_KEY_BUT_MASK_CONTROL) && ksym == 0x0073) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = TIMEDIFF_MS(now, ctrl_s_time);
		if (elapsed < CTRL_S_TIMEOUT_MS) {
		    pty_write(pty, "\x13", 1);
		} else {
		    flags |= FLAG_CTRL_S_PREFIX;
		    ctrl_s_time = now;
		}
		break;
	    }

	    send_key((uint32_t) ((kp->state & XCB_MOD_MASK_CONTROL) ? base_ksym : ksym),
		     kp->state);
	    break;
	}

    case XCB_BUTTON_PRESS:
	{
	    xcb_button_press_event_t *bp = (xcb_button_press_event_t *) ge;
	    xcb_last_time = bp->time;
	    uint mod = bp->state;
	    int col, row;
	    event_coords(bp->event_x, bp->event_y, &col, &row);
	    if (col >= 0 && row >= 0) {
		if (bp->detail == 2) {
		    paste_request(XCB_ATOM_PRIMARY);
		    break;
		}
		input_mouse(input, bp->detail, mod, col, row, MOUSE_PRESS,
			    pty, term);
	    }
	    break;
	}

    case XCB_BUTTON_RELEASE:
	{
	    xcb_button_release_event_t *br =
		(xcb_button_release_event_t *) ge;
	    xcb_last_time = br->time;
	    uint mod = br->state;
	    int col, row;
	    event_coords(br->event_x, br->event_y, &col, &row);
	    if (col >= 0 && row >= 0) {
		input_mouse(input, br->detail, mod, col, row,
			    MOUSE_RELEASE, pty, term);
		if (input_sel_active(input)) {
		    free(sel_buf);
		    sel_buf = term_sel_get(term);
		    sel_len = sel_buf ? strlen(sel_buf) : 0;
		    xcb_set_selection_owner(conn, xcb_win,
					    XCB_ATOM_PRIMARY, br->time);
		    xcb_flush(conn);
		}
	    }
	    break;
	}

    case XCB_MOTION_NOTIFY:
	{
	    xcb_motion_notify_event_t *mn =
		(xcb_motion_notify_event_t *) ge;
	    uint mod = mn->state;
	    int col, row;
	    event_coords(mn->event_x, mn->event_y, &col, &row);
	    if (col >= 0 && row >= 0)
		input_mouse(input, 0, mod, col, row, MOUSE_MOTION, pty,
			    term);
	    break;
	}

    case XCB_CONFIGURE_NOTIFY:
	{
	    xcb_configure_notify_event_t *cn =
		(xcb_configure_notify_event_t *) ge;
	    pending_conf_w = cn->width;
	    pending_conf_h = cn->height;
	    flags |= FLAG_CONF_PENDING;
	    break;
	}

    case XCB_DESTROY_NOTIFY:
	running = 0;
	break;

    case XCB_FOCUS_IN:
	frontend_focus(&renderer, 1);
	flags |= FLAG_WIN_FOCUSED;
	if (term_mode(term, MODE_FOCUS))
	    term_write(term, "\033[I", 3);
	break;

    case XCB_FOCUS_OUT:
	frontend_focus(&renderer, 0);
	flags &= ~FLAG_WIN_FOCUSED;
	if (term_mode(term, MODE_FOCUS))
	    term_write(term, "\033[O", 3);
	break;

    case XCB_CLIENT_MESSAGE:
	running = 0;
	break;

    case XCB_KEY_RELEASE:
    case XCB_MAP_NOTIFY:
    case XCB_UNMAP_NOTIFY:
    case XCB_VISIBILITY_NOTIFY:
    case XCB_MAPPING_NOTIFY:
    case XCB_SELECTION_CLEAR:
	{
	    term_sel_clear(term);
	    break;
	}

    case XCB_SELECTION_NOTIFY:
	{
	    xcb_selection_notify_event_t *sn =
		(xcb_selection_notify_event_t *) ge;
	    if (!(flags & FLAG_SEL_PENDING))
		break;
	    flags &= ~FLAG_SEL_PENDING;
	    if (sn->property == XCB_ATOM_NONE)
		break;
	    xcb_get_property_cookie_t gc =
		xcb_get_property(conn, 0, xcb_win, sn->property,
				 XCB_ATOM_ANY, 0, 0x100000);
	    xcb_get_property_reply_t *r =
		xcb_get_property_reply(conn, gc, NULL);
	    if (r && r->format && r->value_len) {
		size_t n = r->value_len * (r->format / 8);
		pty_write(pty, (const char *) xcb_get_property_value(r),
			  n);
	    }
	    free(r);
	    if (sn->property != XCB_ATOM_PRIMARY
		&& sn->property != XCB_ATOM_STRING)
		xcb_delete_property(conn, xcb_win, sn->property);
	    xcb_flush(conn);
	    break;
	}

    case XCB_SELECTION_REQUEST:
	{
	    xcb_selection_request_event_t *sr =
		(xcb_selection_request_event_t *) ge;
	    if (!sel_buf) {
		xcb_selection_notify_event_t ev = {
		    .response_type = XCB_SELECTION_NOTIFY,
		    .requestor = sr->requestor,
		    .selection = sr->selection,
		    .target = sr->target,
		    .property = XCB_ATOM_NONE,
		    .time = sr->time,
		};
		xcb_send_event(conn, 0, sr->requestor,
			       XCB_EVENT_MASK_NO_EVENT,
			       (const char *) &ev);
		break;
	    }
	    if (sr->target == xcb_sel_targets) {
		xcb_atom_t targets[] = {
		    xcb_sel_targets,
		    xcb_utf8_string ? xcb_utf8_string : XCB_ATOM_STRING,
		    XCB_ATOM_STRING,
		};
		int nt = 3;
		if (!xcb_utf8_string)
		    nt = 2;
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE,
				    sr->requestor, sr->property,
				    XCB_ATOM_ATOM, 32, nt, targets);
	    } else if (sr->target == XCB_ATOM_STRING
		       || sr->target == xcb_utf8_string) {
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE,
				    sr->requestor, sr->property,
				    sr->target, 8, sel_len, sel_buf);
	    } else {
		xcb_selection_notify_event_t ev = {
		    .response_type = XCB_SELECTION_NOTIFY,
		    .requestor = sr->requestor,
		    .selection = sr->selection,
		    .target = sr->target,
		    .property = XCB_ATOM_NONE,
		    .time = sr->time,
		};
		xcb_send_event(conn, 0, sr->requestor,
			       XCB_EVENT_MASK_NO_EVENT,
			       (const char *) &ev);
		break;
	    }
	    xcb_selection_notify_event_t ev = {
		.response_type = XCB_SELECTION_NOTIFY,
		.requestor = sr->requestor,
		.selection = sr->selection,
		.target = sr->target,
		.property = sr->property,
		.time = sr->time,
	    };
	    xcb_send_event(conn, 0, sr->requestor, XCB_EVENT_MASK_NO_EVENT,
			   (const char *) &ev);
	    xcb_flush(conn);
	    break;
	}

    case XCB_PROPERTY_NOTIFY:
    case XCB_REPARENT_NOTIFY:
    case XCB_CREATE_NOTIFY:
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
	break;
    }
}



static int xcb_dpi(const xcb_screen_t *screen)
{
    if (screen->width_in_millimeters > 0)
	return (int) (screen->width_in_pixels * 25.4 /
		      screen->width_in_millimeters + 0.5);
    return 96;
}

int main(int argc, char *argv[])
{
    const char *font_override = NULL;
    char *cmd_argv[256];
    int cmd_argc = 0;
    const char *initial_title = NULL;
    const char *shell_name = NULL;
    const char *term_override = NULL;
    int init_cols = 80, init_rows = 24;


    for (int i = 1; i < argc; i++) {
	if (strncmp(argv[i], "--font=", 7) == 0) {
	    font_override = argv[i] + 7;
	} else if (strncmp(argv[i], "--size=", 7) == 0) {
	    if (sscanf(argv[i] + 7, "%dx%d", &init_cols, &init_rows) < 1 ||
		init_cols < 1 || init_rows < 1) {
		fprintf(stderr, "term: invalid --size format (use WxH)\n");
		return 1;
	    }
	} else if (strncmp(argv[i], "--on-resize=", 12) == 0) {
	    const char *mode = argv[i] + 12;
	    if (strcmp(mode, "resize") == 0)
		flags |= FLAG_ON_RESIZE_RESIZE;
	    else if (strcmp(mode, "adjust") == 0)
		flags &= ~FLAG_ON_RESIZE_RESIZE;
	    else {
		fprintf(stderr,
			"term: --on-resize must be 'adjust' or 'resize'\n");
		return 1;
	    }
	} else if (strcmp(argv[i], "--keep-open") == 0) {
	    flags |= FLAG_OPT_KEEP;
	} else if (strncmp(argv[i], "--term=", 7) == 0) {
	    term_override = argv[i] + 7;
	} else if (strncmp(argv[i], "--command=", 10) == 0) {
	    initial_title = argv[i] + 10;
	    cmd_argv[cmd_argc++] = argv[i] + 10;
	    while (++i < argc)
		cmd_argv[cmd_argc++] = argv[i];
	    cmd_argv[cmd_argc] = NULL;
	    break;
	} else {
	    fprintf(stderr,
		    "Usage: term [--font=fontspec] [--size=WxH]\n"
		    "       [--command=cmd [args...]] [--on-resize=adjust|resize]\n");
	    return 1;
	}
    }


    if (!initial_title) {
	const char *sh = getenv("SHELL");
	if (!sh)
	    sh = "/bin/sh";
	shell_name = strrchr(sh, '/');
	initial_title = shell_name ? shell_name + 1 : sh;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    setlocale(LC_CTYPE, "");
    FcInit();


    conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn))
	die("cannot open display");

    keysyms = xcb_key_symbols_alloc(conn);
    if (!keysyms)
	die("cannot allocate XCB key symbols");

    {
	xcb_get_modifier_mapping_cookie_t mc =
	    xcb_get_modifier_mapping(conn);
	xcb_get_modifier_mapping_reply_t *mr =
	    xcb_get_modifier_mapping_reply(conn, mc, NULL);
	if (mr) {
	    xcb_keycode_t *kcs = (xcb_keycode_t *)
		xcb_get_modifier_mapping_keycodes(mr);
	    int per = mr->keycodes_per_modifier;
	    for (int i = 0; i < 8; i++)
		for (int j = 0; j < per; j++) {
		    xcb_keycode_t kc = kcs[i * per + j];
		    if (kc == 0)
			continue;
		    xcb_keysym_t ks =
			xcb_key_symbols_get_keysym(keysyms, kc, 0);
		    if (ks == XK_ISO_Level3_Shift || ks == XK_Mode_switch)
			mod_switch_mask |= (1 << i);
		}
	    free(mr);
	}
    }

    xcb_screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;


    int dpi = xcb_dpi(xcb_screen);


    current_cc = COLOUR_CORRECTION_DEFAULT;
    palette = palette_new();
    palette_load_fgbg(palette, frontend_default_fg, frontend_default_bg,
		      frontend_default_colourname, PAL_SIZE);

    term = term_new(init_cols, init_rows, palette);
    term_set_allow_alt_screen(term, allowaltscreen);
    term_set_write_fn(term, write_cb, NULL);
    term_set_bell_fn(term, bell_cb, NULL);
    term_set_title_fn(term, title_cb, NULL);
    parser = parser_new();


    frontend_proto =
	frontend_cairo_temp(font_override ? font_override :
			    frontend_default_font, palette, dpi);
    renderer = frontend_proto;
    frontend_resize(&renderer, init_cols, init_rows);

    int winw =
	2 * frontend_border(&renderer) +
	init_cols * frontend_char_width(&renderer);
    int winh =
	2 * frontend_border(&renderer) +
	init_rows * frontend_char_height(&renderer);

    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BIT_GRAVITY |
	XCB_CW_EVENT_MASK;
    uint32_t values[3];
    values[0] = palette_resolve_corrected(palette, PAL_DEFAULT_BG,
					  &current_cc) & 0x00FFFFFF;
    values[1] = XCB_GRAVITY_NORTH_WEST;
    values[2] = XCB_EVENT_MASK_EXPOSURE |
	XCB_EVENT_MASK_KEY_PRESS |
	XCB_EVENT_MASK_KEY_RELEASE |
	XCB_EVENT_MASK_BUTTON_PRESS |
	XCB_EVENT_MASK_BUTTON_RELEASE |
	XCB_EVENT_MASK_BUTTON_MOTION |
	XCB_EVENT_MASK_POINTER_MOTION |
	XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE;

    xcb_win = xcb_generate_id(conn);
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, xcb_win,
		      xcb_screen->root, 0, 0, winw, winh, 0,
		      XCB_WINDOW_CLASS_INPUT_OUTPUT,
		      xcb_screen->root_visual, mask, values);


    xcb_intern_atom_cookie_t net_wm_cookie =
	xcb_intern_atom(conn, 0, 12, "_NET_WM_NAME");
    xcb_intern_atom_cookie_t utf8_cookie =
	xcb_intern_atom(conn, 0, 11, "UTF8_STRING");
    xcb_intern_atom_cookie_t del_cookie =
	xcb_intern_atom(conn, 0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_cookie_t prot_cookie =
	xcb_intern_atom(conn, 0, 12, "WM_PROTOCOLS");
    xcb_intern_atom_cookie_t targets_cookie =
	xcb_intern_atom(conn, 0, 7, "TARGETS");
    xcb_intern_atom_cookie_t sel_prop_cookie =
	xcb_intern_atom(conn, 0, 8, "TERM_SEL");
    xcb_intern_atom_cookie_t timestamp_cookie =
	xcb_intern_atom(conn, 0, 9, "TIMESTAMP");
    xcb_intern_atom_cookie_t clipboard_cookie =
	xcb_intern_atom(conn, 0, 9, "CLIPBOARD");

    xcb_net_wm_name = 0;
    xcb_utf8_string = 0;
    xcb_last_time = XCB_CURRENT_TIME;
    xcb_clipboard = 0;
    sel_buf = NULL;
    sel_len = 0;
    flags &= ~FLAG_SEL_PENDING;

    {
	xcb_intern_atom_reply_t *r =
	    xcb_intern_atom_reply(conn, net_wm_cookie, NULL);
	if (r) {
	    xcb_net_wm_name = r->atom;
	    free(r);
	}
	r = xcb_intern_atom_reply(conn, utf8_cookie, NULL);
	if (r) {
	    xcb_utf8_string = r->atom;
	    free(r);
	}
	r = xcb_intern_atom_reply(conn, targets_cookie, NULL);
	if (r) {
	    xcb_sel_targets = r->atom;
	    free(r);
	}
	r = xcb_intern_atom_reply(conn, sel_prop_cookie, NULL);
	if (r) {
	    xcb_sel_prop = r->atom;
	    free(r);
	}
	r = xcb_intern_atom_reply(conn, timestamp_cookie, NULL);
	if (r)
	    free(r);
	r = xcb_intern_atom_reply(conn, clipboard_cookie, NULL);
	if (r) {
	    xcb_clipboard = r->atom;
	    free(r);
	}
    }


    const char *wt = initial_title ? initial_title : termname;
    snprintf(base_title, sizeof base_title, "%s", wt);
    if (initial_title)
	snprintf(term->title, sizeof(term->title), "%s", initial_title);
    set_window_title(wt);


    {
	xcb_intern_atom_reply_t *del_reply =
	    xcb_intern_atom_reply(conn, del_cookie, NULL);
	xcb_intern_atom_reply_t *prot_reply =
	    xcb_intern_atom_reply(conn, prot_cookie, NULL);

	if (del_reply && prot_reply) {
	    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, xcb_win,
				prot_reply->atom, XCB_ATOM_ATOM, 32, 1,
				&del_reply->atom);
	}
	free(del_reply);
	free(prot_reply);
    }

    xcb_map_window(conn, xcb_win);
    xcb_flush(conn);


    xcb_visualtype_t *visual = NULL;
    xcb_depth_iterator_t di =
	xcb_screen_allowed_depths_iterator(xcb_screen);
    while (di.rem) {
	xcb_visualtype_iterator_t vi = xcb_depth_visuals_iterator(di.data);
	while (vi.rem) {
	    if (vi.data->visual_id == xcb_screen->root_visual) {
		visual = vi.data;
		break;
	    }
	    xcb_visualtype_next(&vi);
	}
	if (visual)
	    break;
	xcb_depth_next(&di);
    }
    if (!visual)
	die("could not find root visual");


    if (frontend_proto.free)
	frontend_proto.free(frontend_proto.ctx);
    FrontendBackendInfo rb_info = {
	.conn = conn,
	.window = xcb_win,
	.visual = visual,
	.scale_to_fit = !(flags & FLAG_ON_RESIZE_RESIZE),
    };
    frontend_proto = frontend_cairo_new(&rb_info, winw, winh,
					font_override ? font_override :
					frontend_default_font, palette,
					dpi);
    renderer = frontend_proto;
    current_cc = colour_regression_ccm(palette);
    frontend_set_colour_correction(&renderer, &current_cc);
    update_back_pixel();
    frontend_resize(&renderer, init_cols, init_rows);

    if (term_override)
	pty_set_term(term_override);
    if (cmd_argc > 0)
	pty = pty_new(cmd_argv[0], cmd_argv);
    else
	pty = pty_new(NULL, NULL);
    if (!pty)
	die("could not create PTY");
    pty_resize(pty, init_cols, init_rows);

    clock_gettime(CLOCK_MONOTONIC, &last_proccheck);
    refresh_proc_prefix();

    input = input_new();

    int xcbfd = xcb_get_file_descriptor(conn);
    int ptyfd = pty_fd(pty);

    clock_gettime(CLOCK_MONOTONIC, &lastblink);
    last_cursor_blink = lastblink;

    struct timespec pt1;

    while (running) {

	struct pollfd fds[2];
	if (!(flags & FLAG_CHILD_EXITED)) {
	    fds[0].fd = ptyfd;
	    fds[0].events = POLLIN;
	} else {
	    fds[0].fd = -1;
	}
	fds[1].fd = xcbfd;
	fds[1].events = POLLIN;

	xcb_generic_event_t *ge;

	int timeout_ms = -1;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	while ((ge = xcb_poll_for_queued_event(conn))) {
	    uint8_t xcb_type = ge->response_type & ~0x80;
	    handle_xcb_event(ge);
	    free(ge);
	    if (!(flags & FLAG_DRAWING) && xcb_type != XCB_KEY_PRESS) {
		draw_trigger = now;
		flags |= FLAG_DRAWING;
	    }
	}
	apply_configure();

	if (flags & FLAG_DRAWING) {
	    double elapsed = TIMEDIFF_MS(now, draw_trigger);
	    timeout_ms = (int) (maxlatency_val - elapsed);
	    if (timeout_ms < 0)
		timeout_ms = 0;

	    if (last_frame_time.tv_sec) {
		double since_frame = TIMEDIFF_MS(now, last_frame_time);
		int frame_wait = (int) (FRAME_TIME_MS - since_frame) + 1;
		if (frame_wait > 0 && frame_wait > timeout_ms)
		    timeout_ms = frame_wait;
	    }
	}

	if (blinktimeout_val || blinktimeout_fast_val) {
	    flags = flag_set(flags, FLAG_BLINK_HAS_SLOW, term_has_blink(term));
	    if (flags & FLAG_BLINK_HAS_SLOW) {
		double belapsed = TIMEDIFF_MS(now, lastblink);
		unsigned int bto = blinktimeout_val;
		int timeout = (int) (bto - belapsed);
		if (timeout_ms < 0 || timeout < timeout_ms)
		    timeout_ms = MAX(0, timeout);
	    }
	}

	{
	    int shape = cursor_shape_current();
	    if ((flags & FLAG_WIN_FOCUSED) && cursor_shape_blinks(shape)) {
		double belapsed = TIMEDIFF_MS(now, last_cursor_blink);
		int timeout = (int) (cursor_tick_delay - belapsed);
		if (timeout_ms < 0 || timeout < timeout_ms)
		    timeout_ms = MAX(0, timeout);
	    }
	}

	int ret = poll(fds, 2, timeout_ms);

	if (xcb_connection_has_error(conn)) {
	    fprintf(stderr, "term: X connection lost\n");
	    running = 0;
	    break;
	}

	if (ret < 0) {
	    if (errno == EINTR) {
		if (sigchld_received) {
		    sigchld_received = 0;
		    int status;
		    if (waitpid(pty_pid(pty), &status, WNOHANG) > 0) {
			if (flags & FLAG_OPT_KEEP)
			    flags |= FLAG_CHILD_EXITED;
			else
			    running = 0;
		    }
		}
		continue;
	    }
	    break;
	}

	clock_gettime(CLOCK_MONOTONIC, &now);

	double proc_elapsed = TIMEDIFF_MS(now, last_proccheck);
	if (proc_elapsed >= 1000.0) {
	    last_proccheck = now;
	    refresh_proc_prefix();
	}

	if (!(flags & FLAG_CHILD_EXITED) && fds[0].revents & (POLLIN | POLLHUP)) {
	    ttyread_handler();
	    if (!(flags & FLAG_DRAWING)) {
		draw_trigger = now;
		flags |= FLAG_DRAWING;
	    }
	}


	ge = xcb_poll_for_event(conn);
	while (ge) {
	    uint8_t xcb_type = ge->response_type & ~0x80;
	    handle_xcb_event(ge);
	    free(ge);
	    ge = xcb_poll_for_event(conn);
	    if (!(flags & FLAG_DRAWING) && xcb_type != XCB_KEY_PRESS) {
		draw_trigger = now;
		flags |= FLAG_DRAWING;
	    }
	}
	apply_configure();

	if (blinktimeout_val || blinktimeout_fast_val) {
	    if (flags & FLAG_BLINK_HAS_SLOW) {
		double belapsed = TIMEDIFF_MS(now, lastblink);
		unsigned int bto = blinktimeout_val;
		if (belapsed >= bto) {
		    term_set_mode(term, !term_mode(term, MODE_BLINK),
				  MODE_BLINK);
		    term_dirty(term);
		    lastblink = now;
		    flags |= FLAG_DRAWING;
		}
	    }
	}

	{
	    int shape = cursor_shape_current();
	    if ((flags & FLAG_WIN_FOCUSED) && cursor_shape_blinks(shape)) {
		double belapsed = TIMEDIFF_MS(now, last_cursor_blink);
		if (belapsed >= cursor_tick_delay) {
		    last_cursor_blink = now;
		    double ms = now.tv_sec * 1000.0 + now.tv_nsec / 1e6;
		    double period = 2.0 * cursor_blinktimeout_val;
		    double phase = fmod(ms, period) / period;
		    double a = 0.5 * (1.0 + cos(2.0 * M_PI * phase));
		    if (fabs(a - cursor_alpha_sent) >= 1.0 / 256.0) {
			cursor_alpha_sent = a;
			frontend_set_cursor_alpha(&renderer, a);
			if (!(flags & FLAG_DRAWING)) {
			    draw_trigger = now;
			    flags |= FLAG_DRAWING;
			}
		    }
		}
	    } else if (cursor_alpha_sent != 1.0) {
		cursor_alpha_sent = 1.0;
		frontend_set_cursor_alpha(&renderer, 1.0);
		if (!(flags & FLAG_DRAWING)) {
		    draw_trigger = now;
		    flags |= FLAG_DRAWING;
		}
	    }
	}

	if (flags & FLAG_CTRL_S_PREFIX) {
	    double selapsed = TIMEDIFF_MS(now, ctrl_s_time);
	    if (selapsed >= CTRL_S_TIMEOUT_MS) {
		flags &= ~FLAG_CTRL_S_PREFIX;
		pty_write(pty, "\x13", 1);
	    }
	}

	if (!(flags & FLAG_DRAWING))
	    continue;

	clock_gettime(CLOCK_MONOTONIC, &pt1);

	double elapsed = TIMEDIFF_MS(pt1, draw_trigger);
	if (elapsed < minlatency_val && !term_sel_active(term)) {
	    int remain = (int) (minlatency_val - elapsed);
	    if (remain > 0)
		usleep((useconds_t) remain * 1000);
	    continue;
	}

	if (last_frame_time.tv_sec) {
	    double since_frame = TIMEDIFF_MS(pt1, last_frame_time);
	    if (since_frame < FRAME_TIME_MS && !term_sel_active(term))
		continue;
	}

	{
	    int sel_active, sel_start_x, sel_start_y, sel_end_x, sel_end_y;
	    term_sel_get_bounds(term, &sel_active, &sel_start_x,
				&sel_start_y, &sel_end_x, &sel_end_y);
	    int frame_mode = term_mode_raw(term);
	    frontend_frame(&renderer, term_screen(term),
			   term_cursor_x(term), term_cursor_y(term),
			   term_cursor_shape(term),
			   frame_mode, sel_active, sel_start_x,
			   sel_start_y, sel_end_x, sel_end_y);
	}

	update_back_pixel();
	frontend_flush(&renderer);
	xcb_flush(conn);
	clock_gettime(CLOCK_MONOTONIC, &last_frame_time);

	flags &= ~FLAG_DRAWING;
    }


    pty_free(pty);
    if (frontend_proto.free)
	frontend_proto.free(frontend_proto.ctx);
    parser_free(parser);
    term_free(term);
    input_free(input);
    palette_free(palette);
    xcb_destroy_window(conn, xcb_win);
    xcb_key_symbols_free(keysyms);
    xcb_disconnect(conn);
    FcFini();

    return 0;
}
