#ifndef REGION_H
#define REGION_H
#include "types.h"

struct Screen;

typedef Cell(*PlaneAt) (const void *ctx, int x, int y);

typedef struct {
    const void *ctx;
    PlaneAt at;
} Plane;

typedef struct {
    const struct Screen *s;
} ScreenPlaneCtx;

Cell screen_plane_at(const void *ctx, int x, int y);

typedef struct {
    int x0, y0, x1, y1;
} Rect;

static inline int rect_width(const Rect *r)
{
    return r->x1 - r->x0;
}

static inline int rect_height(const Rect *r)
{
    return r->y1 - r->y0;
}

static inline int rect_area(const Rect *r)
{
    return (r->x1 - r->x0) * (r->y1 - r->y0);
}

typedef struct {
    Rect bounds;
    int bg;
    unsigned flags;
} Region;

enum {
    REGION_HAS_PRINTABLE = 1 << 0,
};

Region *region_compute(const Plane * p, int cols, int rows,
		       int *nout, uint8_t * visited);
void region_free(Region * r);
#endif
