#ifndef TERM_H
#define TERM_H
#include "libccor/ccor.h"
#include "parser.h"
#include "screen.h"
#include "types.h"



typedef enum {
    MODE_WRAP = 1 << 0,
    MODE_INSERT = 1 << 1,
    MODE_CRLF = 1 << 3,
    MODE_BRACKETED_PASTE = 1 << 4,
    MODE_PRINT = 1 << 5,
    MODE_FOCUS = 1 << 6,
    MODE_APPKEYPAD = 1 << 7,
    MODE_APPCURSOR = 1 << 8,
    MODE_MOUSEBTN = 1 << 9,
    MODE_MOUSEMOTION = 1 << 10,
    MODE_MOUSEX10 = 1 << 11,
    MODE_MOUSEMANY = 1 << 12,
    MODE_MOUSESGR = 1 << 13,
    MODE_HIDE = 1 << 16,
    MODE_BLINK = 1 << 19,
} TermMode;

#define MODE_MOUSE (MODE_MOUSEBTN|MODE_MOUSEMOTION|MODE_MOUSEX10|MODE_MOUSEMANY)


enum {
    CURSOR_SAVE,
    CURSOR_LOAD,
};

enum {
    CURSOR_DEFAULT,
    CURSOR_WRAPNEXT,
    CURSOR_ORIGIN,
    CURSOR_INPUT_NEEDS_WRAP = 4,
};



typedef enum {
    CURSOR_SHAPE_DEFAULT,
    CURSOR_SHAPE_BLOCK_BLINK,
    CURSOR_SHAPE_BLOCK,
    CURSOR_SHAPE_UNDERLINE_BLINK,
    CURSOR_SHAPE_UNDERLINE,
    CURSOR_SHAPE_BAR_BLINK,
    CURSOR_SHAPE_BAR,
} CursorShape;

int cursor_shape_blinks(int shape);



typedef enum {
    SNAP_WORD,
} SelectionSnap;

typedef struct {
    int start_x, start_y;
    int end_x, end_y;

    int origin_x, origin_y;

    unsigned flags;
} Selection;

enum {
    SELECTION_ACTIVE = 1 << 0,
};



typedef struct Term Term;


typedef void (*TermWriteFn)(const char *buf, size_t len, void *userdata);


typedef void (*TermBellFn)(void *userdata);


typedef void (*TermTitleFn)(const char *title, void *userdata);

struct Term {
    Screen *screen;
    Palette *pal;


    int cx, cy;
    Cell curcell;
    int cursor_state;
    CursorShape cursor_shape;


    Cell save_curcell[2];
    int save_cx[2], save_cy[2];
    int save_cursor_state[2];


    int mode;


    int top, bot;


    int *tabstops;
    int ntabstops;


    int cur_charset;
    int sel_charset;
    int charset_table[4];


    Rune last_rune;


    char title[256];
    char clip[4096];


    TermWriteFn write_fn;
    void *write_ctx;


    TermBellFn bell_fn;
    void *bell_ctx;


    TermTitleFn title_fn;
    void *title_ctx;


    unsigned flags;
};

enum {
    TERM_ALLOW_ALT_SCREEN = 1 << 0,
};


Term *term_new(int cols, int rows, Palette * pal);
void term_free(Term * t);


void term_process_batch(Term * t, const Event * ev, int nev);


void term_resize(Term * t, int cols, int rows);


void term_reset(Term * t);


const Screen *term_screen(const Term * t);
void term_dirty(Term * t);
int term_mode(const Term * t, TermMode mode);
int term_mode_raw(const Term * t);
void term_set_mode(Term * t, int set, TermMode mode);
int term_cursor_x(const Term * t);
int term_cursor_y(const Term * t);
int term_cursor_shape(const Term * t);
const char *term_title(const Term * t);


void term_sel_start(Term * t, int x, int y, SelectionSnap snap);
void term_sel_extend(Term * t, int x, int y);
void term_sel_clear(Term * t);
int term_sel_active(const Term * t);
char *term_sel_get(const Term * t);
void term_sel_get_bounds(const Term * t, int *active, int *start_x,
			 int *start_y, int *end_x, int *end_y);

void term_set_write_fn(Term * t, TermWriteFn fn, void *userdata);
void term_set_bell_fn(Term * t, TermBellFn fn, void *userdata);
void term_set_title_fn(Term * t, TermTitleFn fn, void *userdata);
void term_set_allow_alt_screen(Term * t, int allow);
void term_write(Term * t, const char *buf, size_t len);
void term_bell(Term * t);

#endif
