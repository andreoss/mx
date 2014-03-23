#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cairo.h>
#include <cairo-ft.h>
#include <cairo-xcb.h>
#include <hb.h>
#include <hb-ft.h>
#include "config.h"
#include "frontend.h"
#include "region.h"
#include "utf8.h"


#define HB_FIXED_SCALE 64
#define GRAD_LIGHT     1.08
#define GRAD_DARK      0.92
#define FLASH_DECAY    0.88
#define FLASH_MIN      0.01
#define GLOW_LAYERS    3
#define OUTLINE_DIRS   8

static void argb_to_rgb(Argb c, double *r, double *g, double *b)
{
    *r = (double) ((c >> 16) & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX;
    *g = (double) ((c >> 8) & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX;
    *b = (double) (c & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX;
}

static Argb apply_sepia(Argb c)
{
    double r = (c >> 16) & COLOUR_CHANNEL_MASK;
    double g = (c >> 8) & COLOUR_CHANNEL_MASK;
    double b = c & COLOUR_CHANNEL_MASK;
    double nr = r * 0.393 + g * 0.769 + b * 0.189;
    double ng = r * 0.349 + g * 0.686 + b * 0.168;
    double nb = r * 0.272 + g * 0.534 + b * 0.131;
    if (nr > 255) nr = 255;
    if (ng > 255) ng = 255;
    if (nb > 255) nb = 255;
    return ((Argb)(nr + 0.5) << 16) | ((Argb)(ng + 0.5) << 8) | (Argb)(nb + 0.5);
}

static int attr_faint(Attr a)
{
    return (a & ATTR_BOLD_FAINT) == ATTR_FAINT;
}

static const hb_feature_t default_hb_features[] = {
    { HB_TAG('c', 'a', 'l', 't'), 1, HB_FEATURE_GLOBAL_START,
     HB_FEATURE_GLOBAL_END },
    { HB_TAG('l', 'i', 'g', 'a'), 1, HB_FEATURE_GLOBAL_START,
     HB_FEATURE_GLOBAL_END }
};

static void
draw_region_gradient(cairo_t *draw, int xa, int ya, int xb, int yb,
		     Argb bg_c)
{
    cairo_pattern_t *grad = cairo_pattern_create_linear(xa, ya, xa, yb);
    double r0, g0, b0;
    argb_to_rgb(bg_c, &r0, &g0, &b0);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, r0 * 1.08, g0 * 1.08,
				      b0 * 1.08, 1.0);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, r0 * 0.92, g0 * 0.92,
				      b0 * 0.92, 1.0);
    cairo_save(draw);
    cairo_rectangle(draw, xa, ya, xb - xa, yb - ya);
    cairo_clip(draw);
    cairo_set_source(draw, grad);
    cairo_paint(draw);
    cairo_restore(draw);
    cairo_pattern_destroy(grad);
}

#define DEFAULT_HB_FEATURES_COUNT \
    (sizeof(default_hb_features) / sizeof(default_hb_features[0]))

typedef struct {
    FT_Face face;
    hb_font_t *hb_font;
    cairo_font_face_t *cairo_face;
    int pixel_size;
    hb_buffer_t *hb_buf;
    cairo_glyph_t *glyph_buf;
    int glyph_cap;

    char *cache_text;
    int cache_text_len;
    int cache_text_cap;
    uint32_t cache_hash;
    unsigned int cache_glyph_count;
    hb_glyph_info_t *cache_glyph_infos;
} FontResources;





struct Overlay {
    cairo_surface_t *surface;
    cairo_t *cr;
    int x, y, w, h;
    ShadowParams shadow;
    unsigned flags;
};

enum {
    OVERLAY_HAS_SHADOW = 1 << 0,
};



typedef struct {
    cairo_surface_t *output;
    cairo_t *cr;
    const Palette *pal;
    int cols, rows;
    int char_width, char_height;
    int char_ascent;
    int border_px;
    int win_width, win_height;
    char *font_string;
    FT_Library ft_lib;
    double font_size;
    double font_scale;
    ColourCorrection colour_cc;
    cairo_surface_t *backbuf;
    cairo_t *backcr;
    double flash_alpha;
    int expected_width, expected_height;
    double font_outline_alpha;
    Overlay *overlays;
    int noverlays, overlay_cap;
    Argb bg_grad_top;
    int border_gen;
    uint8_t *border_visited;
    Region *border_cache;
    int border_cache_n;

    Cell *drawn;
    uint8_t *repaint;

    int prev_sel_start_x, prev_sel_start_y;
    int prev_sel_end_x, prev_sel_end_y;

    double cursor_alpha;

    int prev_cur_x, prev_cur_y, prev_cur_w, prev_cur_h;

    cairo_pattern_t *dim_left;
    cairo_pattern_t *dim_right;
    double dim_cache_w;
    Argb dim_cache_bell;
    double dim_cache_alpha;

    FontResources normal;
    FontResources bold;

     Argb cc_cache[PAL_SIZE];
    unsigned cc_cache_gen;
    unsigned flags;
} CairoBackend;

enum {
    CAIRO_FOCUS_DIM = 1 << 0,
    CAIRO_SCALE_TO_FIT = 1 << 1,
    CAIRO_FONT_OUTLINE = 1 << 2,
    CAIRO_BORDER_BLOCKS = 1 << 3,
    CAIRO_PREV_SEL_ACTIVE = 1 << 4,
    CAIRO_PREV_MODE_BLINK = 1 << 5,
    CAIRO_FORCE_FULL = 1 << 6,
    CAIRO_WINDOW_DAMAGED = 1 << 7,
    CAIRO_CC_CACHE_VALID = 1 << 8,
};



static FcPattern *try_font_match(const char *family, FcPattern *base_pat,
				 int bold, double pt_size)
{
    FcPattern *pat = FcPatternDuplicate(base_pat);
    FcPatternDel(pat, FC_FAMILY);
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *) family);

    if (bold) {
	FcPatternDel(pat, FC_WEIGHT);
	FcPatternAddInteger(pat, FC_WEIGHT, FC_WEIGHT_BOLD);
    }

    FcConfigSubstitute(0, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    int pixel_size = 0;
    FcPatternGetInteger(pat, FC_PIXEL_SIZE, 0, &pixel_size);
    if (pixel_size > 0 || pt_size > 0) {
	FcPatternDel(pat, FC_PIXEL_SIZE);
	FcPatternAddInteger(pat, FC_PIXEL_SIZE, (int) (pt_size + 0.5));
    }

    FcResult res_fc;
    FcPattern *match = FcFontMatch(0, pat, &res_fc);
    FcPatternDestroy(pat);
    return match;
}

static int family_matches(FcPattern *match, const char *family)
{
    FcChar8 *matched_family = NULL;
    if (FcPatternGetString(match, FC_FAMILY, 0, &matched_family) !=
	FcResultMatch)
	return 0;
    return strcasecmp((const char *) matched_family, family) == 0;
}

static FontResources
font_resources_init(FT_Library lib, const char *font_string, int bold,
		    double pt_size)
{
    FontResources res = { 0 };

    FcPattern *base_pat = FcNameParse((const FcChar8 *) font_string);
    if (!base_pat)
	return res;

    FcChar8 *requested_family_raw = NULL;
    FcPatternGetString(base_pat, FC_FAMILY, 0, &requested_family_raw);
    char *requested_family =
	requested_family_raw ? strdup((char *) requested_family_raw) :
	NULL;

    FcPattern *match = NULL;
    if (requested_family) {
	match = try_font_match(requested_family, base_pat, bold, pt_size);
	if (match && !family_matches(match, requested_family)) {
	    FcPatternDestroy(match);
	    match = NULL;
	}
    }
    if (!match) {
	for (int i = 0;
	     i < sizeof(preferred_fonts) / sizeof(preferred_fonts[0]); i++)
	{
	    match =
		try_font_match(preferred_fonts[i], base_pat, bold,
			       pt_size);
	    if (match && family_matches(match, preferred_fonts[i]))
		break;
	    if (match) {
		FcPatternDestroy(match);
		match = NULL;
	    }
	}
    }
    FcPatternDestroy(base_pat);
    free(requested_family);
    if (!match) {
	fprintf(stderr,
		"font_resources_init: FcFontMatch failed for \"%s\"\n",
		font_string);
	return res;
    }

    FcChar8 *family = NULL;
    FcChar8 *style = NULL;
    FcChar8 *file = NULL;
    int index = 0;
    FcPatternGetString(match, FC_FAMILY, 0, &family);
    FcPatternGetString(match, FC_STYLE, 0, &style);
    FcPatternGetString(match, FC_FILE, 0, &file);
    FcPatternGetInteger(match, FC_INDEX, 0, &index);

    if (!file) {
	FcPatternDestroy(match);
	return res;
    }

    FT_Face face = NULL;
    if (FT_New_Face(lib, (const char *) file, index, &face) != 0) {
	fprintf(stderr, "font_resources_init: FT_New_Face failed for %s\n",
		(char *) file);
	FcPatternDestroy(match);
	return res;
    }

    if (face->num_fixed_sizes > 0) {
	int want = (int) (pt_size + 0.5);
	int best = 0;
	for (int i = 0; i < face->num_fixed_sizes; i++) {
	    if (best == 0 || abs(face->available_sizes[i].height - want)
		< abs(face->available_sizes[best - 1].height - want))
		best = i + 1;
	}
	if (best > 0)
	    FT_Set_Pixel_Sizes(face, 0,
			       face->available_sizes[best - 1].height);
    }

    res.pixel_size = face->size ? face->size->metrics.height / 64 : 0;

    cairo_font_face_t *cairo_face =
	cairo_ft_font_face_create_for_ft_face(face, 0);
    if (cairo_font_face_status(cairo_face) != CAIRO_STATUS_SUCCESS) {
	cairo_font_face_destroy(cairo_face);
	FT_Done_Face(face);
	FcPatternDestroy(match);
	return res;
    }

    hb_font_t *hb_font = hb_ft_font_create_referenced(face);
    if (!hb_font) {
	cairo_font_face_destroy(cairo_face);
	FT_Done_Face(face);
	FcPatternDestroy(match);
	return res;
    }

    hb_font_set_scale(hb_font, pt_size * HB_FIXED_SCALE, pt_size * HB_FIXED_SCALE);

    res.face = face;
    res.hb_font = hb_font;
    res.cairo_face = cairo_face;
    res.hb_buf = hb_buffer_create();
    res.glyph_cap = 256;
    res.glyph_buf = malloc(res.glyph_cap * sizeof(cairo_glyph_t));

    FcPatternDestroy(match);
    return res;
}

static void font_resources_destroy(FontResources *res)
{
    if (!res)
	return;
    if (res->hb_font)
	hb_font_destroy(res->hb_font);
    if (res->cairo_face)
	cairo_font_face_destroy(res->cairo_face);
    if (res->face)
	FT_Done_Face(res->face);
    if (res->hb_buf)
	hb_buffer_destroy(res->hb_buf);
    free(res->glyph_buf);
    free(res->cache_text);
    free(res->cache_glyph_infos);
    res->face = NULL;
    res->hb_font = NULL;
    res->cairo_face = NULL;
    res->hb_buf = NULL;
    res->glyph_buf = NULL;
    res->cache_text = NULL;
    res->cache_glyph_infos = NULL;
}

static double parse_font_size(const char *font_string, double default_size)
{
    double size = default_size;
    if (!font_string)
	return size;
    FcPattern *pat = FcNameParse((const FcChar8 *) font_string);
    if (pat) {
	double s = 0;
	if (FcPatternGetDouble(pat, FC_SIZE, 0, &s) == FcResultMatch)
	    size = s;
	FcPatternDestroy(pat);
    }
    return size;
}

static int parse_font_pixel_size(const char *font_string)
{
    if (!font_string)
	return 0;
    FcPattern *pat = FcNameParse((const FcChar8 *) font_string);
    if (!pat)
	return 0;
    int px = 0;
    FcPatternGetInteger(pat, FC_PIXEL_SIZE, 0, &px);
    FcPatternDestroy(pat);
    return px;
}

static CairoBackend *backend_new(cairo_surface_t *sfc,
				 const char *font_string,
				 const Palette *pal, int dpi)
{
    CairoBackend *b = calloc(1, sizeof(*b));
    if (!b)
	return NULL;
    b->output = sfc;
    b->backbuf = NULL;
    b->backcr = NULL;
    b->cr = cairo_create(sfc);
    if (cairo_status(b->cr) != CAIRO_STATUS_SUCCESS) {
	cairo_destroy(b->cr);
	free(b);
	return NULL;
    }
    b->pal = pal;
    b->border_px = 2;
    b->font_string =
	strdup(font_string ? font_string :
	       "Terminus:size=12:antialias=false");
    if (dpi <= 0)
	dpi = 96;
    double pt_size = parse_font_size(b->font_string, 12);
    int pixel_size = parse_font_pixel_size(b->font_string);
    b->font_size = pixel_size > 0 ? pixel_size : (pt_size * dpi / 72.0);
    b->font_scale = frontend_font_scale;
    b->flags &= ~CAIRO_FOCUS_DIM;
    b->flash_alpha = 0;
    b->flags &= ~CAIRO_SCALE_TO_FIT;
    b->expected_width = 0;
    b->expected_height = 0;
    b->flags = flag_set(b->flags, CAIRO_FONT_OUTLINE, frontend_font_outline);
    b->font_outline_alpha = frontend_font_outline_alpha;
    b->flags = flag_set(b->flags, CAIRO_BORDER_BLOCKS, frontend_border_blocks);
    b->border_visited = NULL;
    b->border_cache = NULL;
    b->border_cache_n = 0;
    b->border_gen = 0;
    b->dim_left = NULL;
    b->dim_right = NULL;
    b->dim_cache_w = 0;
    b->dim_cache_bell = 0;
    b->dim_cache_alpha = -1.0;
    b->cursor_alpha = 1.0;

    if (FT_Init_FreeType(&b->ft_lib) != 0) {
	cairo_destroy(b->cr);
	free(b->font_string);
	free(b);
	return NULL;
    }

    b->normal =
	font_resources_init(b->ft_lib, b->font_string, 0, b->font_size);
    b->bold =
	font_resources_init(b->ft_lib, b->font_string, 1, b->font_size);
    if (!b->normal.cairo_face || !b->bold.cairo_face) {
	font_resources_destroy(&b->normal);
	font_resources_destroy(&b->bold);
	cairo_destroy(b->cr);
	free(b->font_string);
	FT_Done_FreeType(b->ft_lib);
	free(b);
	return NULL;
    }

    if (b->normal.pixel_size > 0) {
	b->font_size = (double) b->normal.pixel_size;
	hb_font_set_scale(b->normal.hb_font, b->font_size * HB_FIXED_SCALE,
			  b->font_size * HB_FIXED_SCALE);
	hb_font_set_scale(b->bold.hb_font, b->font_size * HB_FIXED_SCALE,
			  b->font_size * HB_FIXED_SCALE);
    }

    cairo_save(b->cr);
    cairo_set_font_face(b->cr, b->normal.cairo_face);
    cairo_set_font_size(b->cr, b->font_size);
    cairo_font_extents_t fe;
    cairo_font_extents(b->cr, &fe);
    b->char_height = (int) ceilf(fe.ascent + fe.descent);
    cairo_text_extents_t te;
    cairo_text_extents(b->cr, "W", &te);
    b->char_width = MAX(1, (int) roundf(te.x_advance));
    b->char_ascent = (int) ceilf(fe.ascent);
    cairo_restore(b->cr);

    FT_Face nf = b->normal.face;
    fprintf(stderr,
	    "font: \"%s\" \"%s\" px=%.0f dpi=%d cw=%d ch=%d bitmap=%s\n",
	    nf && nf->family_name ? nf->family_name : "?",
	    nf && nf->style_name ? nf->style_name : "?",
	    b->font_size, dpi, b->char_width, b->char_height,
	    b->normal.pixel_size > 0 ? "yes" : "no");

    b->border_px = (int) (b->char_width * 1.8 + 0.5);
    if (b->border_px < 2)
	b->border_px = 2;
    b->colour_cc = COLOUR_CORRECTION_DEFAULT;
    return b;
}



static void resolve_palette(CairoBackend * b, int idx, Argb * c);
static void set_source_argb(cairo_t * cr, Argb c);
static void cairo_free(void *ctx);
static void cairo_frame(void *ctx, const Screen * s,
			int cx, int cy,
			int cursor_shape, int mode,
			int sel_active, int sel_start_x, int sel_start_y,
			int sel_end_x, int sel_end_y);
static void cairo_resize(void *ctx, int cols, int rows);
static void cairo_resize_window(void *ctx, int winw, int winh);
static int cairo_char_width(void *ctx);
static int cairo_char_height(void *ctx);
static void cairo_set_cc(void *ctx, const ColourCorrection * cc);
static void cairo_set_font_scale(void *ctx, double scale);
static void cairo_set_cursor_alpha(void *ctx, double alpha);
static void cairo_damage(void *ctx);
static Overlay *cairo_overlay_push(void *ctx, int x, int y, int w, int h);
static void cairo_overlay_pop(void *ctx);
static void cairo_overlay_shadow(Overlay * o, const ShadowParams * sp);
static void cairo_bell(void *ctx);
static void cairo_focus(void *ctx, int focused);
static int cairo_border(void *ctx);
static int cairo_expected_width(void *ctx);
static int cairo_expected_height(void *ctx);
static int cairo_actual_width(void *ctx);
static int cairo_actual_height(void *ctx);
static void cairo_flush(void *ctx);

static const FrontendProto cairo_proto = {
    .frame = cairo_frame,
    .resize = cairo_resize,
    .resize_window = cairo_resize_window,
    .char_width = cairo_char_width,
    .char_height = cairo_char_height,
    .border = cairo_border,
    .expected_width = cairo_expected_width,
    .expected_height = cairo_expected_height,
    .actual_width = cairo_actual_width,
    .actual_height = cairo_actual_height,
    .set_colour_correction = cairo_set_cc,
    .set_font_scale = cairo_set_font_scale,
    .set_cursor_alpha = cairo_set_cursor_alpha,
    .damage = cairo_damage,
    .overlay_push = cairo_overlay_push,
    .overlay_pop = cairo_overlay_pop,
    .overlay_set_shadow = cairo_overlay_shadow,
    .bell = cairo_bell,
    .focus = cairo_focus,
    .flush = cairo_flush,
    .free = cairo_free,
};



FrontendProto
frontend_cairo_temp(const char *font_string, const Palette *pal, int dpi)
{
    cairo_surface_t *tmp =
	cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    CairoBackend *b = backend_new(tmp, font_string, pal, dpi);
    if (b && b->output == tmp) {
	cairo_surface_destroy(tmp);
    } else if (!b) {
	cairo_surface_destroy(tmp);
    }
    FrontendProto p = cairo_proto;
    p.ctx = b;
    return p;
}

FrontendProto
frontend_cairo_new(const FrontendBackendInfo *info,
		   int winw, int winh,
		   const char *font_string, const Palette *pal, int dpi)
{
    if (!info)
	return (FrontendProto) {
	.ctx = NULL};
    cairo_surface_t *sfc =
	cairo_xcb_surface_create((xcb_connection_t *) info->conn,
				 (xcb_drawable_t) info->window,
				 (xcb_visualtype_t *) info->visual,
				 winw, winh);
    if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
	return (FrontendProto) {
	.ctx = NULL};
    CairoBackend *b = backend_new(sfc, font_string, pal, dpi);
    if (b) {
	if (info->scale_to_fit)
	    b->flags |= CAIRO_SCALE_TO_FIT;
	else
	    b->flags &= ~CAIRO_SCALE_TO_FIT;
	b->win_width = winw;
	b->win_height = winh;
    }
    FrontendProto p = cairo_proto;
    p.ctx = b;
    return p;
}





static void cairo_free(void *ctx)
{
    CairoBackend *b = ctx;
    if (!b)
	return;
    for (int i = 0; i < b->noverlays; i++) {
	cairo_destroy(b->overlays[i].cr);
	cairo_surface_destroy(b->overlays[i].surface);
    }
    free(b->overlays);
    if (b->backcr)
	cairo_destroy(b->backcr);
    if (b->backbuf)
	cairo_surface_destroy(b->backbuf);
    if (b->dim_left)
	cairo_pattern_destroy(b->dim_left);
    if (b->dim_right)
	cairo_pattern_destroy(b->dim_right);
    cairo_destroy(b->cr);
    font_resources_destroy(&b->normal);
    font_resources_destroy(&b->bold);
    if (b->ft_lib)
	FT_Done_FreeType(b->ft_lib);
    free(b->font_string);
    if (b->border_visited)
	free(b->border_visited);
    free(b->border_cache);
    free(b->drawn);
    free(b->repaint);
    free(b);
}

static void cairo_resize(void *ctx, int cols, int rows)
{
    CairoBackend *b = ctx;
    b->cols = cols;
    b->rows = rows;
    int cw = (int) roundf((float) b->char_width * b->font_scale);
    int ch = (int) roundf((float) b->char_height * b->font_scale);
    b->expected_width = 2 * b->border_px + cols * cw;
    b->expected_height = 2 * b->border_px + rows * ch;

    cairo_surface_t *old_backbuf = b->backbuf;
    cairo_t *old_backcr = b->backcr;

    b->backbuf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
					    b->expected_width,
					    b->expected_height);
    if (cairo_surface_status(b->backbuf) != CAIRO_STATUS_SUCCESS) {
	cairo_surface_destroy(b->backbuf);
	b->backbuf = NULL;
	b->backcr = NULL;
    } else {
	b->backcr = cairo_create(b->backbuf);
	if (cairo_status(b->backcr) != CAIRO_STATUS_SUCCESS) {
	    cairo_destroy(b->backcr);
	    cairo_surface_destroy(b->backbuf);
	    b->backcr = NULL;
	    b->backbuf = NULL;
	}
    }

    if (b->backcr) {
	Argb bgc;
	resolve_palette(b, PAL_DEFAULT_BG, &bgc);
	set_source_argb(b->backcr, bgc);
	cairo_paint(b->backcr);
    }

    if (old_backcr)
	cairo_destroy(old_backcr);
    if (old_backbuf)
	cairo_surface_destroy(old_backbuf);

    b->border_gen = 0;
    if (b->dim_left) {
	cairo_pattern_destroy(b->dim_left);
	b->dim_left = NULL;
    }
    if (b->dim_right) {
	cairo_pattern_destroy(b->dim_right);
	b->dim_right = NULL;
    }
    free(b->border_visited);
    b->border_visited = NULL;
    free(b->border_cache);
    b->border_cache = NULL;
    b->border_cache_n = 0;

    free(b->drawn);
    b->drawn = malloc((size_t) b->cols * b->rows * sizeof(Cell));
    if (b->drawn) {
	Cell sentinel = { .r = (Rune) - 1, .fg = 0, .bg = 0, .attr = 0 };
	for (int i = 0; i < b->cols * b->rows; i++)
	    b->drawn[i] = sentinel;
    }
    free(b->repaint);
    b->repaint = malloc((size_t) b->cols * b->rows);
    if (b->repaint)
	memset(b->repaint, 0, (size_t) b->cols * b->rows);
    b->prev_cur_x = 0;
    b->prev_cur_y = 0;
    b->prev_cur_w = 0;
    b->prev_cur_h = 0;
    b->flags |= CAIRO_FORCE_FULL;
}

static void cairo_resize_window(void *ctx, int winw, int winh)
{
    CairoBackend *b = ctx;
    b->win_width = winw;
    b->win_height = winh;
    cairo_xcb_surface_set_size(b->output, winw, winh);
    b->bg_grad_top = 0;
}

static int cairo_char_width(void *ctx)
{
    CairoBackend *b = ctx;
    return MAX(1, (int) roundf((float) b->char_width * b->font_scale));
}

static int cairo_char_height(void *ctx)
{
    CairoBackend *b = ctx;
    return MAX(1, (int) roundf((float) b->char_height * b->font_scale));
}

static int cairo_border(void *ctx)
{
    return ((CairoBackend *) ctx)->border_px;
}

static int cairo_expected_width(void *ctx)
{
    CairoBackend *b = ctx;
    return 2 * b->border_px + b->cols * MAX(1, (int) roundf((float)
							    b->char_width *
							    b->
							    font_scale));
}

static int cairo_expected_height(void *ctx)
{
    CairoBackend *b = ctx;
    return 2 * b->border_px + b->rows * MAX(1, (int) roundf((float)
							    b->char_height
							    *
							    b->
							    font_scale));
}

static int cairo_actual_width(void *ctx)
{
    return ((CairoBackend *) ctx)->win_width;
}

static int cairo_actual_height(void *ctx)
{
    return ((CairoBackend *) ctx)->win_height;
}

static void cairo_set_cc(void *ctx, const ColourCorrection *cc)
{
    CairoBackend *b = ctx;
    b->colour_cc = *cc;
    b->flags &= ~CAIRO_CC_CACHE_VALID;
}

static void cairo_damage(void *ctx)
{
    ((CairoBackend *) ctx)->flags |= CAIRO_WINDOW_DAMAGED;
}

static void cairo_set_cursor_alpha(void *ctx, double alpha)
{
    CairoBackend *b = ctx;
    if (alpha < 0.0)
	alpha = 0.0;
    if (alpha > 1.0)
	alpha = 1.0;
    b->cursor_alpha = alpha;
}

static void cairo_set_font_scale(void *ctx, double scale)
{
    CairoBackend *b = ctx;
    if (b->font_scale == scale)
	return;
    b->font_scale = scale;
    if (b->cols > 0 && b->rows > 0)
	cairo_resize(b, b->cols, b->rows);
}

static void cairo_overlay_shadow(Overlay *o, const ShadowParams *sp)
{
    if (o) {
	o->flags |= OVERLAY_HAS_SHADOW;
	o->shadow = *sp;
    }
}

static void cairo_flush(void *ctx)
{
    CairoBackend *b = ctx;
    if (b->output)
	cairo_surface_flush(b->output);
}

static void cairo_bell(void *ctx)
{
    CairoBackend *b = ctx;
    b->flash_alpha = 0.4;
}

static void cairo_focus(void *ctx, int focused)
{
    CairoBackend *b = ctx;
    b->flags = flag_set(b->flags, CAIRO_FOCUS_DIM, !focused);
}




static void resolve_palette(CairoBackend * b, int idx, Argb * c);
static void set_source_argb(cairo_t * cr, Argb c);
static void cairo_draw_block_borders(CairoBackend * b, cairo_t * draw,
				     const Screen * s);
static void border_cache_refresh(CairoBackend * b, const Screen * s);

static void cell_effective(Cell c, int *fg, int *bg)
{
    int f = (int) c.fg, g = (int) c.bg;
    if (c.attr & ATTR_REVERSE) {
	int t = f;
	f = g;
	g = t;
    }
    *fg = f;
    *bg = g;
}

static void
draw_hb_span(CairoBackend *b, cairo_t *cr, int px, int py,
	     const char *text, const uint16_t *byte_cell, int text_len,
	     int bold, Argb bg)
{
    FontResources *res = bold ? &b->bold : &b->normal;
    if (!res->hb_font || !res->cairo_face)
	return;

    unsigned int count;
    hb_glyph_info_t *info;

    uint32_t hash = 2166136261u;
    for (int i = 0; i < text_len; i++)
	hash = (hash ^ (unsigned char) text[i]) * 16777619u;

    if (res->cache_text_len == text_len && res->cache_hash == hash
	&& memcmp(res->cache_text, text, (size_t) text_len) == 0) {
	count = res->cache_glyph_count;
	info = res->cache_glyph_infos;
    } else {
	hb_buffer_reset(res->hb_buf);
	hb_buffer_add_utf8(res->hb_buf, text, text_len, 0, -1);
	hb_buffer_guess_segment_properties(res->hb_buf);
	hb_shape(res->hb_font, res->hb_buf, default_hb_features,
		 DEFAULT_HB_FEATURES_COUNT);

	count = hb_buffer_get_length(res->hb_buf);
	info = hb_buffer_get_glyph_infos(res->hb_buf, NULL);

	if (text_len + 1 > res->cache_text_cap) {
	    int newcap =
		res->cache_text_cap ? res->cache_text_cap * 2 : 512;
	    if (newcap < text_len + 1)
		newcap = text_len + 1;
	    char *nt = realloc(res->cache_text, (size_t) newcap);
	    if (!nt)
		return;
	    res->cache_text = nt;
	    hb_glyph_info_t *ng = realloc(res->cache_glyph_infos,
					  (size_t) newcap *
					  sizeof(hb_glyph_info_t));
	    if (!ng)
		return;
	    res->cache_glyph_infos = ng;
	    res->cache_text_cap = newcap;
	}
	memcpy(res->cache_text, text, (size_t) text_len);
	res->cache_text[text_len] = '\0';
	res->cache_text_len = text_len;
	res->cache_hash = hash;
	res->cache_glyph_count = count;
	memcpy(res->cache_glyph_infos, info,
	       count * sizeof(hb_glyph_info_t));
    }

    int glyph_needed = (b->flags & CAIRO_FONT_OUTLINE) ? (int) count * 9 : (int) count;

    if (glyph_needed > res->glyph_cap) {
	int newcap = res->glyph_cap ? res->glyph_cap * 2 : 256;
	while (newcap < glyph_needed)
	    newcap *= 2;
	cairo_glyph_t *ng =
	    realloc(res->glyph_buf, newcap * sizeof(cairo_glyph_t));
	if (!ng)
	    return;
	res->glyph_buf = ng;
	res->glyph_cap = newcap;
    }

    double scaled_w = b->char_width * b->font_scale;
    double base_y = py + b->char_ascent * b->font_scale;

    for (unsigned int i = 0; i < count; i++) {
	unsigned int cluster = info[i].cluster;
	int rel_cell =
	    (cluster < (unsigned int) text_len) ? byte_cell[cluster] : 0;
	res->glyph_buf[i].index = info[i].codepoint;
	res->glyph_buf[i].x = px + rel_cell * scaled_w;
	res->glyph_buf[i].y = base_y;
    }

    if (b->flags & CAIRO_FONT_OUTLINE) {
	static const double off_x[OUTLINE_DIRS] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	static const double off_y[OUTLINE_DIRS] = { -1, -1, -1, 0, 0, 1, 1, 1 };
	Argb oc = colour_luma(bg) < 0.5f ? COLOUR_MASK : 0x000000u;
	oc = colour_correct(&b->colour_cc, oc);
	double or, og, ob;
	argb_to_rgb(oc, &or, &og, &ob);
	for (int k = 0; k < OUTLINE_DIRS; k++) {
	    int base = (int) count + k * (int) count;
	    double dx = off_x[k];
	    double dy = off_y[k];
	    for (unsigned int i = 0; i < count; i++) {
		res->glyph_buf[base + i].index = res->glyph_buf[i].index;
		res->glyph_buf[base + i].x = res->glyph_buf[i].x + dx;
		res->glyph_buf[base + i].y = res->glyph_buf[i].y + dy;
	    }
	}
	cairo_save(cr);
	cairo_set_source_rgba(cr, or, og, ob, b->font_outline_alpha);
	cairo_show_glyphs(cr, res->glyph_buf + count, count * 8);
	cairo_restore(cr);
    }

    cairo_show_glyphs(cr, res->glyph_buf, count);
}



static int
sel_check(int x, int y, int active, int start_x, int start_y,
	  int end_x, int end_y)
{
    if (!active)
	return 0;
    if (y < start_y || y > end_y)
	return 0;
    if (y == start_y && x < start_x)
	return 0;
    if (y == end_y && x > end_x)
	return 0;
    return 1;
}

static int check_edge(const Screen * s, const Region * r,
		      int axis, int pos, int from, int to);
static int is_valid_box(const Screen * s, const Region * r,
			int cols, int rows);

static void
cairo_frame(void *ctx, const Screen *s,
	    int cx, int cy,
	    int cursor_shape, int mode,
	    int sel_active, int sel_start_x, int sel_start_y,
	    int sel_end_x, int sel_end_y)
{
    CairoBackend *b = ctx;
    int x, y, cols = b->cols, rows = b->rows;
    int ch = (int) roundf((float) b->char_height * b->font_scale);
    int cw = (int) roundf((float) b->char_width * b->font_scale);
    int bp = b->border_px;
    int shape = cursor_shape;
    int cursor_hidden = (mode & MODE_HIDE) != 0;
    if (shape == CURSOR_SHAPE_DEFAULT)
	shape = frontend_cursor_shape ? frontend_cursor_shape
	    : CURSOR_SHAPE_BLOCK;

    if (cols <= 0 || rows <= 0)
	return;

    double lw = (double) b->char_height / BASE_CHAR_HEIGHT;
    int cm = (int) (CURSOR_REPAINT_MARGIN * lw);
    if (cm < 1)
	cm = 1;
    cairo_t *draw = b->backcr ? b->backcr : b->cr;

    if (!b->repaint) {
	b->repaint = malloc((size_t) cols * rows);
	if (b->repaint)
	    memset(b->repaint, 0, (size_t) cols * rows);
    }
    if (!b->repaint)
	return;
    int use_diff = b->drawn != NULL;

    Argb bg;
    int bg_switched = 0;
    resolve_palette(b, PAL_DEFAULT_BG, &bg);
    if (bg != b->bg_grad_top) {
	if (b->backcr) {
	    set_source_argb(b->backcr, bg);
	    cairo_paint(b->backcr);
	    bg_switched = 1;
	}
	b->bg_grad_top = bg;
    }

    int dx1 = 0, dy1 = 0, dx2 = cols - 1, dy2 = rows - 1;
    int has_dirty = screen_dirty_get(s, &dx1, &dy1, &dx2, &dy2);
    if (b->flags & CAIRO_FORCE_FULL) {
	b->flags &= ~CAIRO_FORCE_FULL;
	has_dirty = 1;
	dx1 = 0;
	dy1 = 0;
	dx2 = cols - 1;
	dy2 = rows - 1;
    }
    if (bg_switched) {
	if (b->drawn)
	    for (int i = 0; i < cols * rows; i++)
		b->drawn[i].r = (Rune) - 1;
	has_dirty = 1;
	dx1 = 0;
	dy1 = 0;
	dx2 = cols - 1;
	dy2 = rows - 1;
    }

    int sel_changed = (sel_active != ((b->flags & CAIRO_PREV_SEL_ACTIVE) != 0))
	|| (sel_active
	    && (sel_start_x != b->prev_sel_start_x
		|| sel_start_y != b->prev_sel_start_y
		|| sel_end_x != b->prev_sel_end_x
		|| sel_end_y != b->prev_sel_end_y));
    if (sel_changed) {
	if (use_diff) {
	    int ox1 = (b->flags & CAIRO_PREV_SEL_ACTIVE) ? b->prev_sel_start_x : cols;
	    int oy1 = (b->flags & CAIRO_PREV_SEL_ACTIVE) ? b->prev_sel_start_y : rows;
	    int ox2 = (b->flags & CAIRO_PREV_SEL_ACTIVE) ? b->prev_sel_end_x : -1;
	    int oy2 = (b->flags & CAIRO_PREV_SEL_ACTIVE) ? b->prev_sel_end_y : -1;
	    int nx1 = sel_active ? sel_start_x : cols;
	    int ny1 = sel_active ? sel_start_y : rows;
	    int nx2 = sel_active ? sel_end_x : -1;
	    int ny2 = sel_active ? sel_end_y : -1;
	    int ry1 = MIN(oy1, ny1);
	    int ry2 = MAX(oy2, ny2);
	    for (y = ry1; y <= ry2 && y < rows; y++) {
		int rx1 = ox1 <= ox2 ? ox1 : cols;
		int rx2 = ox1 <= ox2 ? ox2 : -1;
		int rnx1 = nx1 <= nx2 ? nx1 : cols;
		int rnx2 = nx1 <= nx2 ? nx2 : -1;
		if (rx2 < rnx1 || rnx2 < rx1) {
		    if (rx1 <= rx2)
			for (x = MAX(rx1, 0); x <= MIN(rx2, cols - 1); x++)
			    b->drawn[y * cols + x].r = (Rune) - 1;
		    if (rnx1 <= rnx2)
			for (x = MAX(rnx1, 0); x <= MIN(rnx2, cols - 1);
			     x++)
			    b->drawn[y * cols + x].r = (Rune) - 1;
		} else {
		    int sx1 = MIN(rx1, rnx1);
		    int sx2 = MAX(rx2, rnx2);
		    for (x = MAX(sx1, 0); x <= MIN(sx2, cols - 1); x++)
			b->drawn[y * cols + x].r = (Rune) - 1;
		}
	    }
	    if (ry1 <= ry2) {
		if (!has_dirty) {
		    dx1 = MAX(MIN(ox1, nx1), 0);
		    dy1 = MAX(ry1, 0);
		    dx2 = MIN(MAX(ox2, nx2), cols - 1);
		    dy2 = MIN(ry2, rows - 1);
		    has_dirty = 1;
		} else {
		    if (MIN(ox1, nx1) < dx1)
			dx1 = MIN(ox1, nx1);
		    if (ry1 < dy1)
			dy1 = ry1;
		    if (MAX(ox2, nx2) > dx2)
			dx2 = MAX(ox2, nx2);
		    if (ry2 > dy2)
			dy2 = ry2;
		}
	    }
	}
	b->flags = flag_set(b->flags, CAIRO_PREV_SEL_ACTIVE, sel_active);
	b->prev_sel_start_x = sel_start_x;
	b->prev_sel_start_y = sel_start_y;
	b->prev_sel_end_x = sel_end_x;
	b->prev_sel_end_y = sel_end_y;
    }

    int blink_now = (mode & MODE_BLINK) != 0;
    if (use_diff
	&& blink_now != ((b->flags & CAIRO_PREV_MODE_BLINK) != 0)
	&& screen_has_blink(s)) {
	dx1 = dy1 = 0;
	dx2 = cols - 1;
	dy2 = rows - 1;
	has_dirty = 1;
	for (int i = 0; i < cols * rows; i++)
	    b->drawn[i].r = (Rune) - 1;
    }
    b->flags = flag_set(b->flags, CAIRO_PREV_MODE_BLINK, blink_now);

    if (!use_diff) {
	has_dirty = 1;
	dx1 = 0;
	dy1 = 0;
	dx2 = cols - 1;
	dy2 = rows - 1;
    }

    LIMIT(dx1, 0, cols - 1);
    LIMIT(dx2, 0, cols - 1);
    LIMIT(dy1, 0, rows - 1);
    LIMIT(dy2, 0, rows - 1);

    Region *regions = NULL;
    int nregions = 0;
    if (has_dirty && (b->flags & CAIRO_BORDER_BLOCKS)) {
	border_cache_refresh(b, s);
	regions = b->border_cache;
	nregions = b->border_cache_n;
    }

    int fx1 = cols, fy1 = rows, fx2 = -1, fy2 = -1;
    if (has_dirty)
	for (y = dy1; y <= dy2; y++) {
	    uint8_t *flags = b->repaint + (size_t) y * cols;
	    Cell *drow = b->drawn + (size_t) y * cols;
	    memset(flags, 0, (size_t) cols);
	    int any = 0;
	    for (x = 0; x < cols; x++) {
		Cell c = screen_get(s, x, y);
		Cell old = drow[x];
		drow[x] = c;
		if (!use_diff || ATTRCMP(c, old) || c.r != old.r) {
		    flags[x] = 1;
		    any = 1;
		}
	    }
	    if (!any)
		continue;

	    x = 0;
	    while (x < cols) {
		Cell first = drow[x];
		if (first.r == 0 || (first.attr & ATTR_WDUMMY)) {
		    x++;
		    continue;
		}
		int x2 = x + 1;
		while (x2 < cols) {
		    Cell next = drow[x2];
		    if (next.r == 0)
			break;
		    if (next.attr & ATTR_WDUMMY) {
			x2++;
			continue;
		    }
		    if (next.attr != first.attr || next.fg != first.fg
			|| next.bg != first.bg || next.ul != first.ul)
			break;
		    if (sel_check
			(x2, y, sel_active, sel_start_x, sel_start_y,
			 sel_end_x, sel_end_y)
			!= sel_check(x, y, sel_active, sel_start_x,
				     sel_start_y, sel_end_x, sel_end_y))
			break;
		    x2++;
		}
		int run_changed = 0;
		for (int i = x; i < x2 && !run_changed; i++)
		    run_changed = flags[i];
		if (run_changed)
		    for (int i = x; i < x2; i++)
			flags[i] = 1;
		x = x2;
	    }

	    for (x = 0; x < cols; x++)
		if (flags[x]) {
		    if (x < fx1)
			fx1 = x;
		    if (x > fx2)
			fx2 = x;
		    if (y < fy1)
			fy1 = y;
		    if (y > fy2)
			fy2 = y;
		}
	}

    if (fx2 >= 0 && nregions > 0)
	for (int i = 0; i < nregions; i++) {
	    Region *r = &regions[i];
	    if (r->bounds.x1 <= fx1 || r->bounds.x0 > fx2 || r->bounds.y1 <= fy1 || r->bounds.y0 > fy2)
		continue;
	    for (int ry = r->bounds.y0; ry < r->bounds.y1; ry++) {
		uint8_t *rflags = b->repaint + (size_t) ry * cols;
		for (int rx = r->bounds.x0; rx < r->bounds.x1; rx++)
		    rflags[rx] = 1;
	    }
	    if (r->bounds.x0 < fx1)
		fx1 = r->bounds.x0;
	    if (r->bounds.x1 - 1 > fx2)
		fx2 = r->bounds.x1 - 1;
	    if (r->bounds.y0 < fy1)
		fy1 = r->bounds.y0;
	    if (r->bounds.y1 - 1 > fy2)
		fy2 = r->bounds.y1 - 1;
	}

    if (fx2 >= 0) {
	cairo_save(draw);
	cairo_rectangle(draw, bp + fx1 * cw, bp + fy1 * ch,
			(fx2 - fx1 + 1) * cw, (fy2 - fy1 + 1) * ch);
	cairo_clip(draw);

	for (y = fy1; y <= fy2; y++) {
	    const uint8_t *flags = b->repaint + (size_t) y * cols;
	    Cell *drow = b->drawn + (size_t) y * cols;
	    x = 0;
	    while (x < cols) {
		if (!flags[x]) {
		    x++;
		    continue;
		}
		Cell c = drow[x];
		int eff_fg, eff_bg;
		cell_effective(c, &eff_fg, &eff_bg);
		int span = 1;
		while (x + span < cols && flags[x + span]) {
		    Cell n = drow[x + span];
		    if (n.attr & ATTR_WDUMMY) {
			span++;
			continue;
		    }
		    int nfg, nbg;
		    cell_effective(n, &nfg, &nbg);
		    if (nbg != eff_bg)
			break;
		    if (sel_check(x + span, y, sel_active, sel_start_x,
				  sel_start_y, sel_end_x, sel_end_y)
			!= sel_check(x, y, sel_active, sel_start_x,
				     sel_start_y, sel_end_x, sel_end_y))
			break;
		    span++;
		}
		Argb cell_bg;
		resolve_palette(b, eff_bg, &cell_bg);
		set_source_argb(draw, cell_bg);
		cairo_rectangle(draw, bp + x * cw, bp + y * ch,
				cw * span, ch);
		cairo_fill(draw);
		x += span;
	    }
	}

	if (nregions > 0) {
	    cairo_save(draw);
	    cairo_new_path(draw);
	    for (y = fy1; y <= fy2; y++) {
		const uint8_t *flags = b->repaint + (size_t) y * cols;
		x = 0;
		while (x < cols) {
		    if (!flags[x]) {
			x++;
			continue;
		    }
		    int xs = x;
		    while (x < cols && flags[x])
			x++;
		    cairo_rectangle(draw, bp + xs * cw, bp + y * ch,
				    (x - xs) * cw, ch);
		}
	    }
	    if (sel_active) {
		cairo_rectangle(draw, bp + sel_start_x * cw,
				bp + sel_start_y * ch,
				(sel_end_x - sel_start_x + 1) * cw,
				(sel_end_y - sel_start_y + 1) * ch);
		cairo_set_fill_rule(draw, CAIRO_FILL_RULE_EVEN_ODD);
	    }
	    cairo_clip(draw);
	    for (int i = 0; i < nregions; i++) {
		Region *r = &regions[i];
		if (!is_valid_box(s, r, cols, rows))
		    continue;
		Argb bg_c;
		resolve_palette(b, r->bg, &bg_c);
		draw_region_gradient(draw, bp + r->bounds.x0 * cw,
				     bp + r->bounds.y0 * ch, bp + r->bounds.x1 * cw,
				     bp + r->bounds.y1 * ch, bg_c);
	    }
	    cairo_restore(draw);
	}

	int cur_bold = -1;
	for (y = fy1; y <= fy2; y++) {
	    const uint8_t *flags = b->repaint + (size_t) y * cols;
	    Cell *drow = b->drawn + (size_t) y * cols;
	    x = 0;
	    while (x < cols) {
		Cell first = drow[x];
		if (first.r == 0 || (first.attr & ATTR_WDUMMY)) {
		    x++;
		    continue;
		}

		int x2 = x + 1;
		while (x2 < cols) {
		    Cell next = drow[x2];
		    if (next.r == 0)
			break;
		    if (next.attr & ATTR_WDUMMY) {
			x2++;
			continue;
		    }
		    if (next.attr != first.attr || next.fg != first.fg
			|| next.bg != first.bg || next.ul != first.ul)
			break;
		    if (sel_check
			(x2, y, sel_active, sel_start_x, sel_start_y,
			 sel_end_x, sel_end_y)
			!= sel_check(x, y, sel_active, sel_start_x,
				     sel_start_y, sel_end_x, sel_end_y))
			break;
		    x2++;
		}

		if (!flags[x]) {
		    x = x2;
		    continue;
		}

		char buf[4096];
		uint16_t byte_cell[4096];
		int pos = 0;
		for (int i = x; i < x2; i++) {
		    Cell c = drow[i];
		    if (c.r == 0 || (c.attr & ATTR_WDUMMY))
			continue;
		    if (pos + 4 < (int) sizeof(buf)) {
			int byte_start = pos;
			if (c.r < 0x80)
			    buf[pos++] = (char) c.r;
			else
			    pos += utf8_encode(c.r, buf + pos);
			for (int bi = byte_start; bi < pos; bi++)
			    byte_cell[bi] = (uint16_t) (i - x);
		    }
		}
		buf[pos] = '\0';

		if (pos > 0) {
		    int px = bp + x * cw;
		    int py = bp + y * ch;
		    int span_sel = sel_check(x, y, sel_active, sel_start_x,
					     sel_start_y, sel_end_x,
					     sel_end_y);

		    int eff_fg, eff_bg;
		    cell_effective(first, &eff_fg, &eff_bg);
		    int bold_idx = eff_fg;
		    if ((first.attr & ATTR_BOLD) && bold_idx >= 0
			&& bold_idx < 8)
			bold_idx += 8;
		    Argb fg;
		    resolve_palette(b, bold_idx, &fg);

		    {
			Argb clamp_bg;
			resolve_palette(b, eff_bg, &clamp_bg);
			fg = colour_min_contrast(fg, clamp_bg,
						 MIN_CONTRAST);
		    }
		    if (span_sel)
			fg = apply_sepia(fg);

		    if ((mode & MODE_BLINK)
			&& (first.attr &
			    (ATTR_BLINK_SLOW | ATTR_BLINK_FAST))) {
			Argb cell_bg;
			resolve_palette(b, eff_bg, &cell_bg);
			fg = cell_bg;
		    }

		    if (first.attr & ATTR_INVISIBLE) {
			cairo_save(draw);
			cairo_set_source_rgba(draw, 0, 0, 0, 1);
			cairo_set_line_width(draw, 1.0);
			int span_w = (x2 - x) * cw;
			cairo_rectangle(draw, px + 0.5, py + 0.5,
					span_w - 1, ch - 1);
			cairo_set_dash(draw, (double[]) { 2, 2 }, 2, 0);
			cairo_stroke(draw);
			cairo_set_dash(draw, NULL, 0, 0);
			cairo_restore(draw);
		    } else {
			double fg_alpha =
			    attr_faint(first.attr) ? FAINT_ALPHA : 1.0;
			{
			    double c_r, c_g, c_b;
			    argb_to_rgb(fg, &c_r, &c_g, &c_b);
			    cairo_set_source_rgba(draw, c_r, c_g, c_b,
						  fg_alpha);
			}

			int span_bold = first.attr & ATTR_BOLD;
			if (span_bold != cur_bold) {
			    FontResources *fr =
				span_bold ? &b->bold : &b->normal;
			    cairo_set_font_face(draw, fr->cairo_face);
			    cairo_set_font_size(draw,
						b->font_size *
						b->font_scale);
			    cur_bold = span_bold;
			}

			{
			    Argb span_bg;
			    resolve_palette(b, eff_bg, &span_bg);
			    draw_hb_span(b, draw, px, py, buf, byte_cell,
					 pos, first.attr & ATTR_BOLD,
					 span_bg);
			}

			if (first.attr & (ATTR_UNDERLINE | ATTR_STRUCK)) {
			    double cx0 = px;
			    double cx1 = px + (x2 - x) * cw;
			    double ul_y =
				py +
				(int) (b->char_ascent * b->font_scale) + 2;
			    double strike_y = py + ch * 0.5;
			    cairo_set_line_width(draw, 2.0);
			    if (first.attr & ATTR_UNDERLINE) {
				if (first.ul != PAL_DEFAULT_FG) {
				    Argb ulc;
				    resolve_palette(b, (int) first.ul, &ulc);
				    double u_r, u_g, u_b;
				    argb_to_rgb(ulc, &u_r, &u_g, &u_b);
				    cairo_set_source_rgba(draw, u_r, u_g, u_b, 1.0);
				}
				cairo_new_path(draw);
				cairo_move_to(draw, cx0, ul_y);
				cairo_line_to(draw, cx1, ul_y);
				cairo_stroke(draw);
			    }
			    if (first.attr & ATTR_STRUCK) {
				cairo_new_path(draw);
				cairo_move_to(draw, cx0, strike_y);
				cairo_line_to(draw, cx1, strike_y);
				cairo_stroke(draw);
			    }
			}
		    }
		}

		x = x2;
	    }
	}

	cairo_restore(draw);
    }

    screen_clean((Screen *) s);

    int scaled = 0;
    double scale_x = 1.0, scale_y = 1.0;
    if (b->backbuf && (b->flags & CAIRO_SCALE_TO_FIT)
	&& b->expected_width > 0 && b->expected_height > 0
	&& b->win_width > 0 && b->win_height > 0
	&& (b->win_width != b->expected_width
	    || b->win_height != b->expected_height)) {
	scaled = 1;
	scale_x = (double) b->win_width / b->expected_width;
	scale_y = (double) b->win_height / b->expected_height;
    }

    int cur_px = -1, cur_py = 0;
    if (!cursor_hidden && b->cursor_alpha > CURSOR_ALPHA_MIN
	&& BETWEEN(cx, 0, cols - 1) && BETWEEN(cy, 0, rows - 1)) {
	cur_px = bp + cx * cw - cm;
	cur_py = bp + cy * ch - cm;
    }
    int partial = fx2 < 0 && !sel_changed && !bg_switched && !scaled
	&& !(b->flags & CAIRO_FOCUS_DIM) && b->flash_alpha == 0 && b->backbuf != NULL
	&& !(b->flags & CAIRO_WINDOW_DAMAGED);
    b->flags &= ~CAIRO_WINDOW_DAMAGED;
    if (partial) {
	int x1 = 0, y1 = 0, x2 = 0, y2 = 0, have = 0;
	if (cur_px >= 0) {
	    x1 = cur_px;
	    y1 = cur_py;
	    x2 = cur_px + cw + 2 * cm;
	    y2 = cur_py + ch + 2 * cm;
	    have = 1;
	}
	if (b->prev_cur_w > 0) {
	    if (!have) {
		x1 = b->prev_cur_x;
		y1 = b->prev_cur_y;
		x2 = b->prev_cur_x + b->prev_cur_w;
		y2 = b->prev_cur_y + b->prev_cur_h;
		have = 1;
	    } else {
		x1 = MIN(x1, b->prev_cur_x);
		y1 = MIN(y1, b->prev_cur_y);
		x2 = MAX(x2, b->prev_cur_x + b->prev_cur_w);
		y2 = MAX(y2, b->prev_cur_y + b->prev_cur_h);
	    }
	}
	cairo_save(b->cr);
	if (have)
	    cairo_rectangle(b->cr, x1, y1, x2 - x1, y2 - y1);
	cairo_clip(b->cr);
    }
    b->prev_cur_x = cur_px;
    b->prev_cur_y = cur_py;
    b->prev_cur_w = cur_px >= 0 ? cw + 2 * cm : 0;
    b->prev_cur_h = cur_px >= 0 ? ch + 2 * cm : 0;

    if (b->backbuf) {
	cairo_surface_flush(b->backbuf);
	cairo_save(b->cr);
	cairo_set_operator(b->cr, CAIRO_OPERATOR_SOURCE);
	if (scaled)
	    cairo_scale(b->cr, scale_x, scale_y);
	cairo_set_source_surface(b->cr, b->backbuf, 0, 0);
	cairo_paint(b->cr);
	cairo_restore(b->cr);

	if (!scaled
	    && (b->win_width > b->expected_width
		|| b->win_height > b->expected_height)) {
	    set_source_argb(b->cr, bg);
	    if (b->win_width > b->expected_width) {
		cairo_rectangle(b->cr, b->expected_width, 0,
				b->win_width - b->expected_width,
				b->win_height);
		cairo_fill(b->cr);
	    }
	    if (b->win_height > b->expected_height) {
		cairo_rectangle(b->cr, 0, b->expected_height,
				b->win_width,
				b->win_height - b->expected_height);
		cairo_fill(b->cr);
	    }
	}
    }

    cairo_save(b->cr);
    if (scaled)
	cairo_scale(b->cr, scale_x, scale_y);

    if (b->flags & CAIRO_BORDER_BLOCKS)
	cairo_draw_block_borders(b, b->cr, s);

    if (sel_active) {
	int sel_miny = MAX(0, sel_start_y);
	int sel_maxy = MIN(rows - 1, sel_end_y);
	double obp = bp;
	Argb sel_red = colour_correct(&b->colour_cc, 0xFF0000);
	double sel_r, sel_g, sel_b;
	argb_to_rgb(sel_red, &sel_r, &sel_g, &sel_b);
	cairo_set_source_rgba(b->cr, sel_r, sel_g, sel_b, 0.5);
	cairo_set_line_width(b->cr, 1.5 * lw);
	cairo_new_path(b->cr);

	for (int iy = sel_miny; iy <= sel_maxy; iy++) {
	    for (int ix = 0; ix < cols; ix++) {
		if (!sel_check
		    (ix, iy, sel_active, sel_start_x, sel_start_y,
		     sel_end_x, sel_end_y))
		    continue;

		double x0 = obp + ix * cw - 0.5;
		double x1 = x0 + cw;
		double y0 = obp + iy * ch - 0.5;
		double y1 = y0 + ch;

		if (iy == sel_miny
		    || !sel_check(ix, iy - 1, sel_active, sel_start_x,
				  sel_start_y, sel_end_x, sel_end_y)) {
		    cairo_move_to(b->cr, x0, y0);
		    cairo_line_to(b->cr, x1, y0);
		}

		if (iy == sel_maxy
		    || !sel_check(ix, iy + 1, sel_active, sel_start_x,
				  sel_start_y, sel_end_x, sel_end_y)) {
		    cairo_move_to(b->cr, x0, y1);
		    cairo_line_to(b->cr, x1, y1);
		}

		if (ix == 0
		    || !sel_check(ix - 1, iy, sel_active, sel_start_x,
				  sel_start_y, sel_end_x, sel_end_y)) {
		    cairo_move_to(b->cr, x0, y0);
		    cairo_line_to(b->cr, x0, y1);
		}

		if (ix == cols - 1
		    || !sel_check(ix + 1, iy, sel_active, sel_start_x,
				  sel_start_y, sel_end_x, sel_end_y)) {
		    cairo_move_to(b->cr, x1, y0);
		    cairo_line_to(b->cr, x1, y1);
		}
	    }
	}
	cairo_stroke(b->cr);
    }

    int cur_translucent = b->cursor_alpha < CURSOR_ALPHA_MAX;
    if (!cursor_hidden && b->cursor_alpha > CURSOR_ALPHA_MIN
	&& BETWEEN(cx, 0, cols - 1) && BETWEEN(cy, 0, rows - 1)) {
	if (cur_translucent)
	    cairo_push_group(b->cr);
	Cell cur_cell = screen_get(s, cx, cy);
	int cur_fg_idx, cur_bg_idx;
	cell_effective(cur_cell, &cur_fg_idx, &cur_bg_idx);
	int bold_idx = cur_fg_idx;
	if ((cur_cell.attr & ATTR_BOLD) && bold_idx >= 0 && bold_idx < 8)
	    bold_idx += 8;
	Argb cell_fg, cell_bg;
	resolve_palette(b, bold_idx, &cell_fg);
	resolve_palette(b, cur_bg_idx, &cell_bg);
	cell_fg = colour_min_contrast(cell_fg, cell_bg, MIN_CONTRAST);
	float fg_lum = colour_luma(cell_fg);
	float bg_lum = colour_luma(cell_bg);
	Argb contrast = fg_lum > bg_lum ? 0x000000u : COLOUR_MASK;
	float lum_avg = (fg_lum + bg_lum) * 0.5f;
	Argb cursor_fill = lum_avg > 0.5f ? 0x404040u : 0xC0C0C0u;
	int px = bp + cx * cw;
	int py = bp + cy * ch;

	char cbuf[8] = { 0 };
	uint16_t cbyte[8] = { 0 };
	int clen = 0;
	if (cur_cell.r == 0)
	    clen = 0;
	else if (cur_cell.r < 0x80) {
	    cbuf[0] = (char) cur_cell.r;
	    clen = 1;
	} else {
	    clen = utf8_encode(cur_cell.r, cbuf);
	}

	{
	    FontResources *crf =
		(cur_cell.attr & ATTR_BOLD) ? &b->bold : &b->normal;
	    cairo_set_font_face(b->cr, crf->cairo_face);
	    cairo_set_font_size(b->cr, b->font_size * b->font_scale);
	}

	if (shape == CURSOR_SHAPE_UNDERLINE_BLINK
	    || shape == CURSOR_SHAPE_UNDERLINE) {
	    set_source_argb(b->cr, cell_fg);
	    if (clen > 0)
		draw_hb_span(b, b->cr, px, py, cbuf, cbyte, clen,
			     cur_cell.attr & ATTR_BOLD, cell_bg);
	    set_source_argb(b->cr, contrast);
	    cairo_set_line_width(b->cr, CURSOR_WIDTH_THICK * lw);
	    cairo_new_path(b->cr);
	    cairo_move_to(b->cr, px, py + ch - 2);
	    cairo_line_to(b->cr, px + cw, py + ch - 2);
	    cairo_stroke(b->cr);
	} else if (shape == CURSOR_SHAPE_BAR_BLINK
		   || shape == CURSOR_SHAPE_BAR) {
	    float ba = colour_luma(cell_bg);
	    int lr = ba > 0.5f ? 0x99 : 0xFF;
	    int lg = ba > 0.5f ? 0x00 : 0x33;
	    int lb = ba > 0.5f ? 0x00 : 0x33;
	    Argb bar_c = (lr << 16) | (lg << 8) | lb;
	    Argb border = ~bar_c & COLOUR_MASK;
	    double t = (double) frontend_cursor_thickness * lw;
	    if (t < 1.0)
		t = 1.0;
	    double bx = (double) px + 0.5;
	    double bar_cx = bx + t * 0.5;
	    set_source_argb(b->cr, bar_c);
	    cairo_set_line_width(b->cr, 1.0);
	    cairo_rectangle(b->cr, bx, (double) py, t, (double) ch);
	    cairo_fill_preserve(b->cr);
	    set_source_argb(b->cr, border);
	    cairo_stroke(b->cr);
	    set_source_argb(b->cr, bar_c);
	    cairo_rectangle(b->cr, bar_cx - t, (double) py, t * 2.0, t);
	    cairo_fill_preserve(b->cr);
	    set_source_argb(b->cr, border);
	    cairo_stroke(b->cr);
	    set_source_argb(b->cr, bar_c);
	    cairo_rectangle(b->cr, bar_cx - t, (double) (py + ch) - t,
			    t * 2.0, t);
	    cairo_fill_preserve(b->cr);
	    set_source_argb(b->cr, border);
	    cairo_stroke(b->cr);
	} else {
	    Argb border = ~cursor_fill & COLOUR_MASK;
	    cairo_set_line_width(b->cr, CURSOR_WIDTH_NORMAL * lw);
	    cairo_rectangle(b->cr, px + 0.5, py + 0.5, cw - 1, ch - 1);
	    set_source_argb(b->cr, cursor_fill);
	    cairo_fill_preserve(b->cr);
	    set_source_argb(b->cr, border);
	    cairo_stroke(b->cr);
	    if (clen > 0)
		draw_hb_span(b, b->cr, px, py, cbuf, cbyte, clen,
			     cur_cell.attr & ATTR_BOLD, cell_bg);
	}

	if (cur_translucent) {
	    cairo_pop_group_to_source(b->cr);
	    cairo_paint_with_alpha(b->cr, b->cursor_alpha);
	}
    }

    cairo_restore(b->cr);

    if (b->flags & CAIRO_FOCUS_DIM && b->win_width > 0 && b->win_height > 0) {
	float fa = b->flash_alpha;
	Argb bell_c = palette_get(b->pal, 1);
	double w = b->win_width, h = b->win_height;
	double bw = w * 0.15;

	if (w != b->dim_cache_w || bell_c != b->dim_cache_bell
	    || fa != b->dim_cache_alpha) {
	    float r = (float) ((bell_c >> 16) & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX_F;
	    float g = (float) ((bell_c >> 8) & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX_F;
	    float bl = (float) (bell_c & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX_F;

	    if (b->dim_left)
		cairo_pattern_destroy(b->dim_left);
	    b->dim_left = cairo_pattern_create_linear(0, 0, bw, 0);
	    cairo_pattern_add_color_stop_rgba(b->dim_left, 0.0, r, g, bl,
					      fa);
	    cairo_pattern_add_color_stop_rgba(b->dim_left, 1.0, r, g, bl,
					      0.0);

	    if (b->dim_right)
		cairo_pattern_destroy(b->dim_right);
	    b->dim_right = cairo_pattern_create_linear(w - bw, 0, w, 0);
	    cairo_pattern_add_color_stop_rgba(b->dim_right, 0.0, r, g, bl,
					      0.0);
	    cairo_pattern_add_color_stop_rgba(b->dim_right, 1.0, r, g, bl,
					      fa);

	    b->dim_cache_w = w;
	    b->dim_cache_bell = bell_c;
	    b->dim_cache_alpha = fa;
	}

	cairo_rectangle(b->cr, 0, 0, bw, h);
	cairo_set_source(b->cr, b->dim_left);
	cairo_fill(b->cr);
	cairo_rectangle(b->cr, w - bw, 0, bw, h);
	cairo_set_source(b->cr, b->dim_right);
	cairo_fill(b->cr);

	b->flash_alpha *= FLASH_DECAY;
	if (b->flash_alpha < FLASH_MIN)
	    b->flash_alpha = 0;
    } else {
	b->flash_alpha = 0;
    }

    if (partial)
	cairo_restore(b->cr);

    cairo_surface_flush(b->output);
}

static Overlay *cairo_overlay_push(void *ctx, int x, int y, int w, int h)
{
    CairoBackend *b = ctx;
    if (b->noverlays >= b->overlay_cap) {
	b->overlay_cap = b->overlay_cap ? b->overlay_cap * 2 : 4;
	Overlay *tmp = realloc(b->overlays,
			       b->overlay_cap * sizeof(b->overlays[0]));
	if (!tmp)
	    return NULL;
	b->overlays = tmp;
    }
    Overlay *o = &b->overlays[b->noverlays++];
    o->x = x;
    o->y = y;
    o->w = w;
    o->h = h;
    o->surface = cairo_surface_create_similar(b->output,
					      CAIRO_CONTENT_COLOR_ALPHA, w,
					      h);
    o->cr = cairo_create(o->surface);
    o->flags &= ~OVERLAY_HAS_SHADOW;
    return o;
}

static void cairo_overlay_pop(void *ctx)
{
    CairoBackend *b = ctx;
    if (b->noverlays <= 0)
	return;
    Overlay *o = &b->overlays[--b->noverlays];

    if (o->flags & OVERLAY_HAS_SHADOW) {
	int r = o->shadow.radius;
	int dx = o->shadow.dx, dy = o->shadow.dy;
	cairo_push_group(b->cr);
	cairo_set_source_rgba(b->cr, 0, 0, 0, o->shadow.alpha);
	cairo_set_operator(b->cr, CAIRO_OPERATOR_SOURCE);
	cairo_mask_surface(b->cr, o->surface, dx, dy);
	cairo_pop_group_to_source(b->cr);
	cairo_set_operator(b->cr, CAIRO_OPERATOR_OVER);
	cairo_paint(b->cr);

	if (r > 0) {
	    cairo_surface_t *shadow =
		cairo_surface_create_similar(b->output,
					     CAIRO_CONTENT_ALPHA,
					     o->w + 2 * r, o->h + 2 * r);
	    cairo_t *cr2 = cairo_create(shadow);
	    cairo_set_source_rgba(cr2, 0, 0, 0, 1);
	    cairo_mask_surface(cr2, o->surface, r + dx, r + dy);
	    cairo_destroy(cr2);
	    cairo_set_source_surface(b->cr, shadow, o->x - r, o->y - r);
	    cairo_paint(b->cr);
	    cairo_surface_destroy(shadow);
	}
    }

    cairo_set_source_surface(b->cr, o->surface, o->x, o->y);
    cairo_paint(b->cr);

    cairo_destroy(o->cr);
    cairo_surface_destroy(o->surface);
    o->cr = NULL;
    o->surface = NULL;
}

static void
glow_edge_h(cairo_t *draw, int xa, int xb, int y, int dir,
	    double r, double g, double b)
{
    for (int i = 0; i < GLOW_LAYERS; i++) {
	double o = (double) (y + glow_offsets[i] * dir) + 0.5;
	cairo_set_source_rgba(draw, r, g, b, glow_alpha[i]);
	cairo_move_to(draw, (double) xa + 0.5, o);
	cairo_line_to(draw, (double) xb + 0.5, o);
	cairo_stroke(draw);
    }
}

static void
glow_edge_v(cairo_t *draw, int ya, int yb, int x, int dir,
	    double r, double g, double b)
{
    for (int i = 0; i < GLOW_LAYERS; i++) {
	double o = (double) (x + glow_offsets[i] * dir) + 0.5;
	cairo_set_source_rgba(draw, r, g, b, glow_alpha[i]);
	cairo_move_to(draw, o, (double) ya + 0.5);
	cairo_line_to(draw, o, (double) yb + 0.5);
	cairo_stroke(draw);
    }
}

static int
check_edge(const Screen *s, const Region *r,
	   int axis, int pos, int from, int to)
{
    if (axis == 0) {
	if (pos < 0 || pos >= screen_rows(s))
	    return 0;
	for (int t = from; t < to; t++) {
	    Cell n = screen_get(s, t, pos);
	    if (n.bg == r->bg)
		return 0;
	}
    } else {
	if (pos < 0 || pos >= screen_cols(s))
	    return 0;
	for (int t = from; t < to; t++) {
	    Cell n = screen_get(s, pos, t);
	    if (n.bg == r->bg)
		return 0;
	}
    }
    return 1;
}

static int
is_valid_box(const Screen *s, const Region *r, int cols, int rows)
{
    Rect b = r->bounds;
    if (!(r->flags & REGION_HAS_PRINTABLE))
	return 0;
    if (rect_width(&b) < 3)
	return 0;
    if (b.x0 == 0 && b.y0 == 0 && b.x1 == cols && b.y1 == rows)
	return 0;
    if (rect_area(&b) > cols * rows * 3 / 4)
	return 0;
    if (b.y0 > 0 && !check_edge(s, r, 0, b.y0 - 1, b.x0, b.x1))
	return 0;
    if (b.y1 < rows && !check_edge(s, r, 0, b.y1, b.x0, b.x1))
	return 0;
    if (b.x0 > 0 && !check_edge(s, r, 1, b.x0 - 1, b.y0, b.y1))
	return 0;
    if (b.x1 < cols && !check_edge(s, r, 1, b.x1, b.y0, b.y1))
	return 0;
    return 1;
}

static void border_cache_refresh(CairoBackend *b, const Screen *s)
{
    int gen = screen_border_gen(s);
    if (gen == b->border_gen && b->border_cache)
	return;
    b->border_gen = gen;
    free(b->border_cache);
    b->border_cache = NULL;
    b->border_cache_n = 0;
    size_t needed = (size_t) b->cols * b->rows;
    if (!b->border_visited)
	b->border_visited = malloc(needed);
    if (!b->border_visited)
	return;
    ScreenPlaneCtx sp = { s };
    Plane plane = { &sp, screen_plane_at };
    b->border_cache = region_compute(&plane, b->cols, b->rows,
				     &b->border_cache_n,
				     b->border_visited);
}

static void
cairo_draw_block_borders(CairoBackend *b, cairo_t *draw, const Screen *s)
{
    if (!(b->flags & CAIRO_BORDER_BLOCKS))
	return;
    int cols = b->cols, rows = b->rows;
    int cw = (int) roundf((float) b->char_width * b->font_scale);
    int ch = (int) roundf((float) b->char_height * b->font_scale);
    int bp = b->border_px;

    border_cache_refresh(b, s);

    Region *regions = b->border_cache;
    int n = b->border_cache_n;
    if (!regions)
	return;

    cairo_set_line_width(draw, 1.0);

    for (int i = 0; i < n; i++) {
	Region *r = &regions[i];
	if (!is_valid_box(s, r, cols, rows))
	    continue;

	Argb bg_c;
	resolve_palette(b, r->bg, &bg_c);
	Argb border_c = (((bg_c >> 16) & COLOUR_CHANNEL_MASK) >> 1) << 16 |
	    (((bg_c >> 8) & COLOUR_CHANNEL_MASK) >> 1) << 8 | ((bg_c & COLOUR_CHANNEL_MASK) >> 1);
	double rc, gc, bc;
	argb_to_rgb(border_c, &rc, &gc, &bc);

	int xa = bp + r->bounds.x0 * cw;
	int ya = bp + r->bounds.y0 * ch;
	int xb = bp + r->bounds.x1 * cw;
	int yb = bp + r->bounds.y1 * ch;

	glow_edge_h(draw, xa, xb, ya, -1, rc, gc, bc);
	glow_edge_h(draw, xa, xb, yb, 1, rc, gc, bc);
	glow_edge_v(draw, ya, yb, xa, -1, rc, gc, bc);
	glow_edge_v(draw, ya, yb, xb, 1, rc, gc, bc);
    }
}

static void resolve_palette(CairoBackend *b, int idx, Argb *c)
{
    if (IS_TRUECOLOUR((Argb) idx)) {
	*c = palette_resolve_corrected(b->pal, idx, &b->colour_cc);
	return;
    }
    if (!(b->flags & CAIRO_CC_CACHE_VALID)
	|| b->cc_cache_gen != palette_generation(b->pal)) {
	for (int i = 0; i < PAL_SIZE; i++)
	    b->cc_cache[i] = colour_correct(&b->colour_cc,
					    palette_resolve(b->pal,
							    (Argb) i));
	b->cc_cache_gen = palette_generation(b->pal);
	b->flags |= CAIRO_CC_CACHE_VALID;
    }
    if (idx >= 0 && idx < PAL_SIZE)
	*c = b->cc_cache[idx];
    else
	*c = palette_resolve_corrected(b->pal, idx, &b->colour_cc);
}

static void set_source_argb(cairo_t *cr, Argb c)
{
    double r, g, b;
    argb_to_rgb(c, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, 1.0);
}
