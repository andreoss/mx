#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "region.h"
#include "screen.h"

Cell screen_plane_at(const void *ctx, int x, int y)
{
    const ScreenPlaneCtx *sp = ctx;
    return screen_get(sp->s, x, y);
}

static int cell_bg(const Plane *p, int x, int y)
{
    return p->at(p->ctx, x, y).bg;
}

Region *region_compute(const Plane *p, int cols, int rows,
		       int *nout, uint8_t *visited)
{
    int alloc_visited = 0;
    if (!visited) {
	visited = calloc((size_t) cols * rows, 1);
	if (!visited) {
	    *nout = 0;
	    return NULL;
	}
	alloc_visited = 1;
    } else {
	memset(visited, 0, (size_t) cols * rows);
    }

    Region *regions = NULL;
    int nregions = 0;
    int cap = 0;

    for (int y = 0; y < rows; y++) {
	for (int x = 0; x < cols; x++) {
	    int idx = y * cols + x;
	    if (visited[idx])
		continue;

	    int bg = cell_bg(p, x, y);

	    int x0 = x;
	    while (x0 < cols && cell_bg(p, x0, y) == bg)
		x0++;

	    int y0 = y;
	    while (y0 < rows) {
		int ok = 1;
		for (int tx = x; tx < x0; tx++) {
		    if (cell_bg(p, tx, y0) != bg) {
			ok = 0;
			break;
		    }
		}
		if (!ok)
		    break;
		y0++;
	    }

	    int has_printable = 0;
	    for (int ry = y; ry < y0; ry++) {
		for (int rx = x; rx < x0; rx++) {
		    if (!has_printable) {
			Cell c = p->at(p->ctx, rx, ry);
			if (c.r > 0 && c.r < 0x110000)
			    has_printable = 1;
		    }
		    visited[ry * cols + rx] = 1;
		}
	    }

	    if (nregions >= cap) {
		cap = cap ? cap * 2 : 16;
		Region *tmp =
		    realloc(regions, (size_t) cap * sizeof(Region));
		if (!tmp) {
		    if (alloc_visited)
			free(visited);
		    free(regions);
		    *nout = 0;
		    return NULL;
		}
		regions = tmp;
	    }

	    regions[nregions++] = (Region) {
		.bounds = {x, y, x0, y0}, .bg = bg, .flags = has_printable ? REGION_HAS_PRINTABLE : 0};
	}
    }

    if (alloc_visited)
	free(visited);
    *nout = nregions;
    return regions;
}

void region_free(Region *r)
{
    free(r);
}
