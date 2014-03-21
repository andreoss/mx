#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "region.h"
#include "types.h"

static int passed, failed;
static Cell grid[20][20];

static Cell grid_at(const void *ctx, int x, int y)
{
    UNUSED(ctx);
    return grid[y][x];
}

static void fill_grid(int cols, int rows, Cell c)
{
    for (int y = 0; y < rows; y++)
	for (int x = 0; x < cols; x++)
	    grid[y][x] = c;
}

static void test_region_single(void)
{
    fill_grid(5, 5, (Cell) {
	      .r = 'A', .attr = 0, .fg = 7, .bg = 0});
    Plane p = { NULL, grid_at };
    int n = 0;
    Region *r = region_compute(&p, 5, 5, &n, NULL);
    if (!r) {
	printf("  FAIL  region_single: null\n");
	failed++;
	return;
    }
    if (n != 1) {
	printf("  FAIL  region_single: n=%d (want 1)\n", n);
	failed++;
	region_free(r);
	return;
    }
    if (r[0].bounds.x0 != 0 || r[0].bounds.y0 != 0 || r[0].bounds.x1 != 5 || r[0].bounds.y1 != 5) {
	printf("  FAIL  region_single: coords %d,%d,%d,%d\n", r[0].bounds.x0,
	       r[0].bounds.y0, r[0].bounds.x1, r[0].bounds.y1);
	failed++;
    } else {
	printf("  PASS  region_single\n");
	passed++;
    }
    region_free(r);
}

static void test_two_bg(void)
{
    for (int y = 0; y < 3; y++)
	for (int x = 0; x < 4; x++)
	    grid[y][x] = (Cell) {
	    .r = 'A', .attr = 0, .fg = 7, .bg = x < 2 ? 0 : 1};
    Plane p = { NULL, grid_at };
    int n = 0;
    Region *r = region_compute(&p, 4, 3, &n, NULL);
    if (!r) {
	printf("  FAIL  two_bg: null\n");
	failed++;
	return;
    }
    if (n != 2) {
	printf("  FAIL  two_bg: n=%d (want 2)\n", n);
	failed++;
    } else {
	printf("  PASS  two_bg\n");
	passed++;
    }
    region_free(r);
}

static void test_empty_cells(void)
{
    fill_grid(3, 3, (Cell) {
	      .r = 0, .attr = 0, .fg = 0, .bg = 0});
    Plane p = { NULL, grid_at };
    int n = 0;
    Region *r = region_compute(&p, 3, 3, &n, NULL);
    if (!r) {
	printf("  FAIL  empty_cells: null\n");
	failed++;
	return;
    }
    if (n != 1) {
	printf("  FAIL  empty_cells: n=%d (want 1)\n", n);
	failed++;
    } else {
	printf("  PASS  empty_cells\n");
	passed++;
    }
    region_free(r);
}

static void test_checkerboard(void)
{
    for (int y = 0; y < 4; y++)
	for (int x = 0; x < 4; x++)
	    grid[y][x] = (Cell) {
	    .r = 'X', .attr = 0, .fg = 7, .bg = (x + y) % 2};
    Plane p = { NULL, grid_at };
    int n = 0;
    Region *r = region_compute(&p, 4, 4, &n, NULL);
    if (!r) {
	printf("  FAIL  checkerboard: null\n");
	failed++;
	return;
    }
    if (n != 16) {
	printf("  FAIL  checkerboard: n=%d (want 16)\n", n);
	failed++;
    } else {
	printf("  PASS  checkerboard\n");
	passed++;
    }
    region_free(r);
}

static void test_all_spaces(void)
{
    fill_grid(3, 3, (Cell) {
	      .r = ' ', .attr = 0, .fg = 7, .bg = 0});
    Plane p = { NULL, grid_at };
    int n = 0;
    Region *r = region_compute(&p, 3, 3, &n, NULL);
    if (!r) {
	printf("  FAIL  all_spaces: null\n");
	failed++;
	return;
    }
    if (n != 1) {
	printf("  FAIL  all_spaces: n=%d (want 1)\n", n);
	failed++;
    } else {
	printf("  PASS  all_spaces\n");
	passed++;
    }
    region_free(r);
}

static void test_vertical_bands(void)
{
    for (int y = 0; y < 4; y++)
	for (int x = 0; x < 6; x++)
	    grid[y][x] = (Cell) {
	    .r = 'X', .attr = 0, .fg = 7, .bg = x / 2 + 1};
    Plane p = { NULL, grid_at };
    int n = 0;
    Region *r = region_compute(&p, 6, 4, &n, NULL);
    if (!r) {
	printf("  FAIL  vertical_bands: null\n");
	failed++;
	return;
    }
    if (n != 3) {
	printf("  FAIL  vertical_bands: n=%d (want 3)\n", n);
	failed++;
    } else {
	printf("  PASS  vertical_bands\n");
	passed++;
    }
    region_free(r);
}

static void test_horizontal_bands(void)
{
    for (int y = 0; y < 6; y++)
	for (int x = 0; x < 4; x++)
	    grid[y][x] = (Cell) {
	    .r = 'X', .attr = 0, .fg = 7, .bg = y / 2 + 4};
    Plane p = { NULL, grid_at };
    int n = 0;
    Region *r = region_compute(&p, 4, 6, &n, NULL);
    if (!r) {
	printf("  FAIL  horizontal_bands: null\n");
	failed++;
	return;
    }
    if (n != 3) {
	printf("  FAIL  horizontal_bands: n=%d (want 3)\n", n);
	failed++;
    } else {
	printf("  PASS  horizontal_bands\n");
	passed++;
    }
    region_free(r);
}

int main(void)
{
    passed = failed = 0;
    memset(grid, 0, sizeof(grid));
    printf("=== region.c tests ===\n");
    test_region_single();
    test_two_bg();
    test_empty_cells();
    test_checkerboard();
    test_all_spaces();
    test_vertical_bands();
    test_horizontal_bands();
    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return !!failed;
}
