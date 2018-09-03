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
    }

    size_t ncells = (size_t) cols * rows;
    int *bg = malloc(ncells * sizeof(int));
    if (!bg) {
	if (alloc_visited)
	    free(visited);
	*nout = 0;
	return NULL;
    }

    for (int y = 0; y < rows; y++) {
	size_t row = (size_t) y * cols;
	for (int x = 0; x < cols; x++) {
	    Cell c = p->at(p->ctx, x, y);
	    bg[row + x] = c.bg;
	    visited[row + x] = (c.r > 0 && c.r < 0x110000) ? 2 : 0;
	}
    }

    Region *regions = NULL;
    int nregions = 0;
    int cap = 0;

    for (int y = 0; y < rows; y++) {
	size_t row = (size_t) y * cols;
	for (int x = 0; x < cols; x++) {
	    if (visited[row + x] & 1)
		continue;

	    int bgc = bg[row + x];

	    int x0 = x;
	    while (x0 < cols && bg[row + x0] == bgc)
		x0++;

	    int y0 = y;
	    while (y0 < rows) {
		size_t erow = (size_t) y0 * cols;
		int ok = 1;
		for (int tx = x; tx < x0; tx++) {
		    if (bg[erow + tx] != bgc) {
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
		size_t rrow = (size_t) ry * cols;
		for (int rx = x; rx < x0; rx++) {
		    if (visited[rrow + rx] & 2)
			has_printable = 1;
		    visited[rrow + rx] |= 1;
		}
	    }

	    if (nregions >= cap) {
		cap = cap ? cap * 2 : 16;
		Region *tmp =
		    realloc(regions, (size_t) cap * sizeof(Region));
		if (!tmp) {
		    free(bg);
		    if (alloc_visited)
			free(visited);
		    free(regions);
		    *nout = 0;
		    return NULL;
		}
		regions = tmp;
	    }

	    regions[nregions++] = (Region) {
		.bounds = {x, y, x0, y0}, .bg = bgc, .flags = has_printable ? REGION_HAS_PRINTABLE : 0};
	}
    }

    free(bg);
    if (alloc_visited)
	free(visited);
    *nout = nregions;
    return regions;
}

void region_free(Region *r)
{
    free(r);
}
