#include "frontend.h"

void frontend_frame(const FrontendProto *r, const Screen *s,
		    int cx, int cy, int cursor_shape, int mode,
		    int sel_active, int sel_start_x, int sel_start_y,
		    int sel_end_x, int sel_end_y)
{
    r->frame(r->ctx, s, cx, cy, cursor_shape, mode,
	     sel_active, sel_start_x, sel_start_y, sel_end_x, sel_end_y);
}

void frontend_resize(const FrontendProto *r, int cols, int rows)
{
    r->resize(r->ctx, cols, rows);
}

void frontend_resize_window(const FrontendProto *r, int winw, int winh)
{
    r->resize_window(r->ctx, winw, winh);
}

int frontend_char_width(const FrontendProto *r)
{
    return r->char_width(r->ctx);
}

int frontend_char_height(const FrontendProto *r)
{
    return r->char_height(r->ctx);
}

int frontend_border(const FrontendProto *r)
{
    return r->border(r->ctx);
}

int frontend_expected_width(const FrontendProto *r)
{
    return r->expected_width(r->ctx);
}

int frontend_expected_height(const FrontendProto *r)
{
    return r->expected_height(r->ctx);
}

int frontend_actual_width(const FrontendProto *r)
{
    return r->actual_width(r->ctx);
}

int frontend_actual_height(const FrontendProto *r)
{
    return r->actual_height(r->ctx);
}

void frontend_set_colour_correction(const FrontendProto *r,
				    const ColourCorrection *cc)
{
    r->set_colour_correction(r->ctx, cc);
}

void frontend_set_font_scale(const FrontendProto *r, double scale)
{
    if (r->set_font_scale)
	r->set_font_scale(r->ctx, scale);
}

void frontend_set_cursor_alpha(const FrontendProto *r, double alpha)
{
    if (r->set_cursor_alpha)
	r->set_cursor_alpha(r->ctx, alpha);
}

void frontend_damage(const FrontendProto *r)
{
    if (r->damage)
	r->damage(r->ctx);
}

void frontend_bell(const FrontendProto *r)
{
    if (r->bell)
	r->bell(r->ctx);
}

void frontend_focus(const FrontendProto *r, int focused)
{
    if (r->focus)
	r->focus(r->ctx, focused);
}

void frontend_flush(const FrontendProto *r)
{
    if (r->flush)
	r->flush(r->ctx);
}
