#ifndef CONFIG_H
#define CONFIG_H

#include "libccor/ccor.h"
#include <X11/keysym.h>

#if !defined(attribute_unused)
#define attribute_unused __attribute__((unused))
#endif

typedef struct {
    uint32_t keysym;
    unsigned int mask;
    char *s;
    signed char appkey;
    signed char appcursor;
} Key;

#define XK_ANY_MOD    0xFFFFFFFFU

static char *termname attribute_unused = "xterm";
static int allowaltscreen attribute_unused = 1;

static Key key[] attribute_unused = {
    { XK_Up, XK_ANY_MOD, "\033[A", 0, -1 },
    { XK_Down, XK_ANY_MOD, "\033[B", 0, -1 },
    { XK_Right, XK_ANY_MOD, "\033[C", 0, -1 },
    { XK_Left, XK_ANY_MOD, "\033[D", 0, -1 },

    { XK_Up, XK_ANY_MOD, "\033OA", 0, 1 },
    { XK_Down, XK_ANY_MOD, "\033OB", 0, 1 },
    { XK_Right, XK_ANY_MOD, "\033OC", 0, 1 },
    { XK_Left, XK_ANY_MOD, "\033OD", 0, 1 },

    { XK_F1, XK_ANY_MOD, "\033OP", 0, 0 },
    { XK_F2, XK_ANY_MOD, "\033OQ", 0, 0 },
    { XK_F3, XK_ANY_MOD, "\033OR", 0, 0 },
    { XK_F4, XK_ANY_MOD, "\033OS", 0, 0 },
    { XK_F5, XK_ANY_MOD, "\033[15~", 0, 0 },
    { XK_F6, XK_ANY_MOD, "\033[17~", 0, 0 },
    { XK_F7, XK_ANY_MOD, "\033[18~", 0, 0 },
    { XK_F8, XK_ANY_MOD, "\033[19~", 0, 0 },
    { XK_F9, XK_ANY_MOD, "\033[20~", 0, 0 },
    { XK_F10, XK_ANY_MOD, "\033[21~", 0, 0 },
    { XK_F11, XK_ANY_MOD, "\033[23~", 0, 0 },
    { XK_F12, XK_ANY_MOD, "\033[24~", 0, 0 },

    { XK_Home, XK_ANY_MOD, "\033[1~", 0, 0 },
    { XK_End, XK_ANY_MOD, "\033[4~", 0, 0 },
    { XK_Insert, XK_ANY_MOD, "\033[2~", 0, 0 },
    { XK_Delete, XK_ANY_MOD, "\033[3~", 0, 0 },
    { XK_Page_Up, XK_ANY_MOD, "\033[5~", 0, 0 },
    { XK_Page_Down, XK_ANY_MOD, "\033[6~", 0, 0 },
    { XK_BackSpace, XK_ANY_MOD, "\177", 0, 0 },
    { XK_Tab, XK_ANY_MOD, "\011", 0, 0 },
    { XK_ISO_Left_Tab, XK_ANY_MOD, "\033[Z", 0, 0 },
    { XK_Escape, XK_ANY_MOD, "\033", 0, 0 },
    { XK_Return, XK_ANY_MOD, "\r", 0, 0 },
};

static const char *frontend_default_font attribute_unused =
    "Terminus:size=12:antialias=false";
static unsigned frontend_cursor_shape attribute_unused = 5;
static unsigned frontend_cursor_thickness attribute_unused = 2;
static int frontend_font_outline attribute_unused = 1;
static double frontend_font_outline_alpha attribute_unused = 0.35;
static int frontend_border_blocks attribute_unused = 1;
static double frontend_font_scale attribute_unused = 1.0;

static Argb frontend_default_fg attribute_unused = 0x000000;
static Argb frontend_default_bg attribute_unused = 0xFFFFEA;

static const char *frontend_default_colourname[16] attribute_unused = {
    "#1A1A1A",
    "#CC372E",
    "#26A439",
    "#CDAC08",
    "#0869CB",
    "#9647BF",
    "#479EC2",
    "#98989D",
    "#464646",
    "#FF453A",
    "#32D74B",
    "#E5BC00",
    "#0A84FF",
    "#BF5AF2",
    "#69C9F2",
    "#FFFFFF",
};

#define FAINT_ALPHA 0.5
#define CURSOR_WIDTH_NORMAL 1.5
#define CURSOR_WIDTH_THICK 3.0
#define MIN_CONTRAST 4.5f
#define CURSOR_ALPHA_MIN 0.004
#define CURSOR_ALPHA_MAX 0.996
#define FRAME_TIME_MS 16.7
#define CURSOR_REPAINT_MARGIN 4
#define BASE_CHAR_HEIGHT 16.0

extern const double glow_alpha[3];
extern const int glow_offsets[3];
extern const char *preferred_fonts[4];

#define MIN_LATENCY_MS 0.0
#define MAX_LATENCY_MS 8.0
#define BLINK_TIMEOUT_MS 800u
#define BLINK_FAST_TIMEOUT_MS 200u
#define CURSOR_BLINK_TIMEOUT_MS 500u
#define CURSOR_TICK_DELAY_MS 40.0
#define CURSOR_FADE_TICK_MS 40.0
#define FONT_SCALE_INIT 1.0
#define CTRL_S_TIMEOUT_MS 2000
#define TABS 8

#define TERM_TYPE "xterm-256color"
#endif
