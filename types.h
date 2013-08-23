#ifndef TYPES_H
#define TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libccor/ccor.h"

typedef uint32_t Rune;
typedef uint32_t Attr;
enum {
    ATTR_BOLD = 1 << 0,
    ATTR_FAINT = 1 << 1,
    ATTR_ITALIC = 1 << 2,
    ATTR_UNDERLINE = 1 << 3,
    ATTR_BLINK_SLOW = 1 << 4,
    ATTR_BLINK_FAST = 1 << 13,
    ATTR_BLINK = ATTR_BLINK_SLOW | ATTR_BLINK_FAST,
    ATTR_REVERSE = 1 << 5,
    ATTR_INVISIBLE = 1 << 6,
    ATTR_STRUCK = 1 << 7,
    ATTR_WDUMMY = 1 << 10,
    ATTR_BOLD_FAINT = ATTR_BOLD | ATTR_FAINT,
};

enum {
    SGR_RESET = 0,
    SGR_BOLD = 1,
    SGR_FAINT = 2,
    SGR_ITALIC = 3,
    SGR_UNDERLINE = 4,
    SGR_BLINK_SLOW = 5,
    SGR_BLINK_FAST = 6,
    SGR_REVERSE = 7,
    SGR_INVISIBLE = 8,
    SGR_STRUCK = 9,
    SGR_NORMAL = 22,
    SGR_NOT_ITALIC = 23,
    SGR_NOT_UNDERLINE = 24,
    SGR_NOT_BLINK = 25,
    SGR_NOT_REVERSE = 27,
    SGR_NOT_INVISIBLE = 28,
    SGR_NOT_STRUCK = 29,
    SGR_FG_EXTENDED = 38,
    SGR_DEFAULT_FG = 39,
    SGR_BG_EXTENDED = 48,
    SGR_DEFAULT_BG = 49,
    SGR_UL_COLOUR = 58,
    SGR_DEFAULT_UL = 59,
};

#define SGR_FG_BASE   30
#define SGR_BG_BASE   40
#define SGR_FG_BRIGHT 90
#define SGR_BG_BRIGHT 100
#define ANSI_COLOURS  8

typedef struct {
    Rune r;
    Attr attr;
    Argb fg;
    Argb bg;
    Argb ul;
} Cell;
typedef Cell *Line;
#define LEN(a)        (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#define MAX(a, b)     ((a) > (b) ? (a) : (b))
#define BETWEEN(x, a, b) ((a) <= (x) && (x) <= (b))
#define LIMIT(x, a, b)   ((x) = (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x))))
#define DEFAULT(a, b)    ((a) = (a) ? (a) : (b))
#define MODBIT(x, set, bit) ((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))
#define UNUSED(x)     ((void) (x))

static inline unsigned flag_set(unsigned flags, unsigned bit, int on)
{
    return on ? (flags | bit) : (flags & ~bit);
}
#define TIMEDIFF_MS(t1, t2) (((t1).tv_sec - (t2).tv_sec) * 1000.0 + \
	((t1).tv_nsec - (t2).tv_nsec) / 1e6)
#define UTF_SIZ           4
#define ATTRCMP(a, b) ((a).attr != (b).attr || (a).fg != (b).fg || \
		       (a).bg != (b).bg || (a).ul != (b).ul)
#endif
