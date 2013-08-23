#ifndef FRONTEND_H
#define FRONTEND_H
#include <stdlib.h>
#include "libccor/ccor.h"
#include "screen.h"
#include "term.h"
#include "types.h"


typedef struct {
    int radius;
    int dx, dy;
    double alpha;
} ShadowParams;


typedef struct Overlay Overlay;


typedef struct FrontendBackendInfo {
    void *conn;
    unsigned window;
    void *visual;
    int scale_to_fit;
} FrontendBackendInfo;



typedef struct FrontendProto {
    void *ctx;

    void (*frame)(void *ctx, const Screen * s,
		  int cx, int cy,
		  int cursor_shape, int mode,
		  int sel_active, int sel_start_x, int sel_start_y,
		  int sel_end_x, int sel_end_y);
    void (*resize)(void *ctx, int cols, int rows);
    void (*resize_window)(void *ctx, int winw, int winh);
    int (*char_width)(void *ctx);
    int (*char_height)(void *ctx);
    int (*border)(void *ctx);
    int (*expected_width)(void *ctx);
    int (*expected_height)(void *ctx);
    int (*actual_width)(void *ctx);
    int (*actual_height)(void *ctx);
    void (*set_colour_correction)(void *ctx, const ColourCorrection * cc);
    void (*set_font_scale)(void *ctx, double scale);
    void (*set_cursor_alpha)(void *ctx, double alpha);
    void (*damage)(void *ctx);
    Overlay *(*overlay_push)(void *ctx, int x, int y, int w, int h);
    void (*overlay_pop)(void *ctx);
    void (*overlay_set_shadow)(Overlay * o, const ShadowParams * sp);
    void (*bell)(void *ctx);
    void (*focus)(void *ctx, int focused);
    void (*flush)(void *ctx);
    void (*free)(void *ctx);
} FrontendProto;

void frontend_frame(const FrontendProto * r, const Screen * s,
		    int cx, int cy, int cursor_shape, int mode,
		    int sel_active, int sel_start_x, int sel_start_y,
		    int sel_end_x, int sel_end_y);
void frontend_resize(const FrontendProto * r, int cols, int rows);
void frontend_resize_window(const FrontendProto * r, int winw, int winh);
int frontend_char_width(const FrontendProto * r);
int frontend_char_height(const FrontendProto * r);
int frontend_border(const FrontendProto * r);
int frontend_expected_width(const FrontendProto * r);
int frontend_expected_height(const FrontendProto * r);
int frontend_actual_width(const FrontendProto * r);
int frontend_actual_height(const FrontendProto * r);
void frontend_set_colour_correction(const FrontendProto * r,
				    const ColourCorrection * cc);
void frontend_set_font_scale(const FrontendProto * r, double scale);
void frontend_set_cursor_alpha(const FrontendProto * r, double alpha);
void frontend_damage(const FrontendProto * r);
void frontend_bell(const FrontendProto * r);
void frontend_focus(const FrontendProto * r, int focused);
void frontend_flush(const FrontendProto * r);




FrontendProto frontend_cairo_temp(const char *font_string,
				  const Palette * pal, int dpi);


FrontendProto frontend_cairo_new(const FrontendBackendInfo * info,
				 int winw, int winh,
				 const char *font_string,
				 const Palette * pal, int dpi);







#endif
