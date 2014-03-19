#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccor.h"

#define BETWEEN(x, a, b) ((a) <= (x) && (x) <= (b))

#define XTERM_BASE 16
#define XTERM_CUBE 6
#define XTERM_GREY 232
#define GREY_BASE  0x0808
#define GREY_STEP  0x0a0a
#define SIXD_BASE  0x3737
#define SIXD_STEP  0x2828

const ColourCorrection COLOUR_CORRECTION_DEFAULT = {
    .gamma = 1.0f,
    .brightness = 1.0f,
    .contrast = 1.0f,
    .tint = 0x00000000,
    .saturation = 1.0f,
    .flags = 0,
    .space = COLOUR_SPACE_NORMAL,
    .ccm = { { 1, 0, 0}, { 0, 1, 0}, { 0, 0, 1} },
    
};

struct Palette {
    Argb table[PAL_SIZE];
    Argb defaults[PAL_SIZE];
    Argb fg;
    Argb bg;
    Argb cs;
    int ncolours;
    unsigned gen;
};

unsigned palette_generation(const Palette *p)
{
    return p->gen;
}

Palette *palette_new(void)
{
    return calloc(1, sizeof(Palette));
}

void palette_free(Palette *p)
{
    free(p);
}

static uint16_t sixd_to_16bit(int x)
{
    return x == 0 ? 0 : SIXD_BASE + SIXD_STEP * x;
}

float linear_to_srgb(float c)
{
    if (c <= 0.0031308f)
	c *= 12.92f;
    else
	c = 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
    return c * COLOUR_CHANNEL_MAX_F;
}

static float luma(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

static float srgb_u8_lut[256];
static int srgb_u8_ready;

static float srgb_u8(unsigned v)
{
    if (!srgb_u8_ready) {
	for (int i = 0; i < 256; i++)
	    srgb_u8_lut[i] = srgb_to_linear((float) i);
	srgb_u8_ready = 1;
    }
    return srgb_u8_lut[v & COLOUR_CHANNEL_MASK];
}

float colour_luma(Argb c)
{
    return luma(srgb_u8((c >> 16) & COLOUR_CHANNEL_MASK),
		srgb_u8((c >> 8) & COLOUR_CHANNEL_MASK), srgb_u8(c & COLOUR_CHANNEL_MASK));
}

float srgb_to_linear(float c)
{
    c /= COLOUR_CHANNEL_MAX_F;
    if (c <= 0.04045f)
	return c / 12.92f;
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

void apply_luminosity(float *lr, float *lg, float *lb, float target_lum)
{
    float cur = luma(*lr, *lg, *lb);
    if (cur < 0.0001f) {
	*lr = target_lum * 0.2126f;
	*lg = target_lum * 0.7152f;
	*lb = target_lum * 0.0722f;
	return;
    }
    float s = target_lum / cur;
    *lr *= s;
    *lg *= s;
    *lb *= s;
}

static const float xterm16_linear[16][3] = {
    { 0.000000f, 0.000000f, 0.000000f },
    { 0.458275f, 0.000000f, 0.000000f },
    { 0.000000f, 0.458275f, 0.000000f },
    { 0.458275f, 0.458275f, 0.000000f },
    { 0.000000f, 0.000000f, 0.458275f },
    { 0.458275f, 0.000000f, 0.458275f },
    { 0.000000f, 0.458275f, 0.458275f },
    { 0.802078f, 0.802078f, 0.802078f },
    { 0.219815f, 0.219815f, 0.219815f },
    { 1.000000f, 0.000000f, 0.000000f },
    { 0.000000f, 1.000000f, 0.000000f },
    { 1.000000f, 1.000000f, 0.000000f },
    { 0.174903f, 0.174903f, 1.000000f },
    { 1.000000f, 0.000000f, 1.000000f },
    { 0.000000f, 1.000000f, 1.000000f },
    { 1.000000f, 1.000000f, 1.000000f },
};

void
palette_load(Palette *p, const char *colourname[], int ncolours,
	     Argb default_fg, Argb default_bg)
{
    Argb fg = default_fg;
    Argb bg = default_bg;
    if (default_fg < (unsigned) ncolours && colourname
	&& colourname[default_fg]) {
	Argb c = 0;
	if (colour_parse(colourname[default_fg], &c))
	    fg = c;
    }
    if (default_bg < (unsigned) ncolours && colourname
	&& colourname[default_bg]) {
	Argb c = 0;
	if (colour_parse(colourname[default_bg], &c))
	    bg = c;
    }
    palette_load_fgbg(p, fg, bg, colourname, ncolours);
}

static void fill_16_from_xterm(Palette *p, Argb fg, Argb bg)
{
    float fgr = srgb_to_linear((fg >> 16) & COLOUR_CHANNEL_MASK);
    float fgg = srgb_to_linear((fg >> 8) & COLOUR_CHANNEL_MASK);
    float fgb = srgb_to_linear(fg & COLOUR_CHANNEL_MASK);
    float bgr = srgb_to_linear((bg >> 16) & COLOUR_CHANNEL_MASK);
    float bgg = srgb_to_linear((bg >> 8) & COLOUR_CHANNEL_MASK);
    float bgb = srgb_to_linear(bg & COLOUR_CHANNEL_MASK);

    float lum_bg = luma(bgr, bgg, bgb);
    float lum_fg = luma(fgr, fgg, fgb);

    for (int i = 0; i < 16; i++) {
	float lr = xterm16_linear[i][0];
	float lg = xterm16_linear[i][1];
	float lb = xterm16_linear[i][2];
	float target_lum = lum_fg + (lum_bg - lum_fg) * (i / 15.0f);

	apply_luminosity(&lr, &lg, &lb, target_lum);
	lr = fminf(1.0f, fmaxf(0.0f, lr));
	lg = fminf(1.0f, fmaxf(0.0f, lg));
	lb = fminf(1.0f, fmaxf(0.0f, lb));

	p->table[i] = ((uint32_t) (linear_to_srgb(lr) + 0.5f) << 16) |
	    ((uint32_t) (linear_to_srgb(lg) + 0.5f) << 8) |
	    ((uint32_t) (linear_to_srgb(lb) + 0.5f));
	p->defaults[i] = p->table[i];
    }
}

void
palette_load_fgbg(Palette *p, Argb fg, Argb bg,
		  const char *colourname[], int ncolours)
{
    p->ncolours = ncolours;
    p->fg = fg;
    p->bg = bg;
    p->cs = COLOUR_MASK;

    if (fg <= COLOUR_MASK)
	p->fg = fg;
    else
	p->fg = 0xCCCCCC;

    if (bg <= COLOUR_MASK)
	p->bg = bg;
    else
	p->bg = 0x202020;

    fill_16_from_xterm(p, fg, bg);

    if (colourname) {
	for (int i = 0; i < 16 && i < ncolours; i++) {
	    if (colourname[i]) {
		Argb c = 0;
		if (colour_parse(colourname[i], &c)) {
		    p->table[i] = c;
		    p->defaults[i] = c;
		}
	    }
	}
    }

    for (int i = 0; i < ncolours && i < PAL_SIZE; i++) {
	if (BETWEEN(i, XTERM_BASE, COLOUR_CHANNEL_MAX)) {
	    if (i < XTERM_GREY) {
		int r = ((i - XTERM_BASE) / (XTERM_CUBE * XTERM_CUBE)) % XTERM_CUBE;
		int g = ((i - XTERM_BASE) / XTERM_CUBE) % XTERM_CUBE;
		int b = (i - XTERM_BASE) % XTERM_CUBE;
		p->table[i] = p->defaults[i] =
		    (sixd_to_16bit(r) >> 8) << 16 |
		    (sixd_to_16bit(g) >> 8) << 8 | (sixd_to_16bit(b) >> 8);
	    } else {
		int grey = GREY_BASE + GREY_STEP * (i - XTERM_GREY);
		int v = grey >> 8;
		p->table[i] = p->defaults[i] = (v << 16) | (v << 8) | v;
	    }
	}
    }

    p->table[PAL_DEFAULT_FG] = p->fg;
    p->table[PAL_DEFAULT_BG] = p->bg;
    p->table[PAL_DEFAULT_CS] = p->cs;
    p->defaults[PAL_DEFAULT_FG] = p->fg;
    p->defaults[PAL_DEFAULT_BG] = p->bg;
    p->defaults[PAL_DEFAULT_CS] = p->cs;
    p->gen++;
}

void palette_set(Palette *p, int idx, Argb colour)
{
    if (BETWEEN(idx, 0, PAL_SIZE - 1)) {
	p->table[idx] = colour;
	p->gen++;
    }
}

void palette_reload(Palette *p)
{
    memcpy(p->table, p->defaults, sizeof(p->defaults));
    p->gen++;
}

Argb palette_get(const Palette *p, int idx)
{
    if (BETWEEN(idx, 0, PAL_SIZE - 1))
	return p->table[idx];
    return 0;
}

void palette_reset(Palette *p, int idx)
{
    if (BETWEEN(idx, 0, PAL_SIZE - 1)) {
	p->table[idx] = p->defaults[idx];
	p->gen++;
    }
}

Argb palette_resolve(const Palette *p, Argb raw)
{
    if (IS_TRUECOLOUR(raw)) {
	return (TRUERED(raw) << 16) | (TRUEGREEN(raw) << 8) |
	    TRUEBLUE(raw);
    }
    int idx = (int) raw;
    if (idx == PAL_DEFAULT_FG)
	return p->fg;
    if (idx == PAL_DEFAULT_BG)
	return p->bg;
    if (idx == PAL_DEFAULT_CS)
	return p->cs;
    if (BETWEEN(idx, 0, p->ncolours - 1))
	return p->table[idx];
    return 0;
}

#define CCM_ANCHOR_WEIGHT 8
#define CCM_MAX_SAMPLES (16 + 2 * CCM_ANCHOR_WEIGHT)

static void
solve_least_squares(float M[4][3], float A[][4], float Y[][3], int n)
{
    for (int col = 0; col < 3; col++) {
	float ATA[4][4];
	for (int i = 0; i < 4; i++)
	    for (int j = 0; j < 4; j++) {
		float s = 0;
		for (int k = 0; k < n; k++)
		    s += A[k][i] * A[k][j];
		ATA[i][j] = s;
	    }

	float ATy[4];
	for (int i = 0; i < 4; i++) {
	    float s = 0;
	    for (int k = 0; k < n; k++)
		s += A[k][i] * Y[k][col];
	    ATy[i] = s;
	}

	for (int i = 0; i < 4; i++) {
	    for (int j = i + 1; j < 4; j++) {
		if (fabsf(ATA[j][i]) > fabsf(ATA[i][i])) {
		    for (int k = 0; k < 4; k++) {
			float t = ATA[i][k];
			ATA[i][k] = ATA[j][k];
			ATA[j][k] = t;
		    }
		    float t2 = ATy[i];
		    ATy[i] = ATy[j];
		    ATy[j] = t2;
		}
	    }
	    float piv = ATA[i][i];
	    if (fabsf(piv) < 1e-9f)
		piv = (piv < 0) ? -1e-9f : 1e-9f;
	    for (int j = i; j < 4; j++)
		ATA[i][j] /= piv;
	    ATy[i] /= piv;
	    for (int k = 0; k < 4; k++) {
		if (k == i)
		    continue;
		float factor = ATA[k][i];
		for (int j = i; j < 4; j++)
		    ATA[k][j] -= factor * ATA[i][j];
		ATy[k] -= factor * ATy[i];
	    }
	}

	for (int i = 0; i < 4; i++)
	    M[i][col] = ATy[i];
    }
}

static int
ccm_add_sample(float A[][4], float Y[][3], int n, Argb in,
	       const float *out)
{
    A[n][0] = srgb_u8((in >> 16) & COLOUR_CHANNEL_MASK);
    A[n][1] = srgb_u8((in >> 8) & COLOUR_CHANNEL_MASK);
    A[n][2] = srgb_u8((in >> 0) & COLOUR_CHANNEL_MASK);
    A[n][3] = 1.0f;
    Y[n][0] = out ? out[0] : A[n][0];
    Y[n][1] = out ? out[1] : A[n][1];
    Y[n][2] = out ? out[2] : A[n][2];
    return n + 1;
}

ColourCorrection colour_regression_ccm(const Palette *p)
{
    float A[CCM_MAX_SAMPLES][4];
    float Y[CCM_MAX_SAMPLES][3];
    int n = 0;
    for (int row = 0; row < 16; row++)
	n = ccm_add_sample(A, Y, n, p->table[row], xterm16_linear[row]);

    for (int i = 0; i < CCM_ANCHOR_WEIGHT; i++) {
	n = ccm_add_sample(A, Y, n, p->fg, NULL);
	n = ccm_add_sample(A, Y, n, p->bg, NULL);
    }

    float M[4][3];
    solve_least_squares(M, A, Y, n);

    ColourCorrection cc;
    memset(&cc, 0, sizeof(cc));
    cc.ccm[0][0] = M[0][0];
    cc.ccm[0][1] = M[0][1];
    cc.ccm[0][2] = M[0][2];
    cc.ccm[1][0] = M[1][0];
    cc.ccm[1][1] = M[1][1];
    cc.ccm[1][2] = M[1][2];
    cc.ccm[2][0] = M[2][0];
    cc.ccm[2][1] = M[2][1];
    cc.ccm[2][2] = M[2][2];

    float scale_r = M[3][0];
    float scale_g = M[3][1];
    float scale_b = M[3][2];

    cc.brightness = powf(2.0f, (scale_r + scale_g + scale_b) / 3.0f);
    cc.gamma = 1.0f;
    cc.contrast = 1.0f;
    cc.saturation = 1.0f;
    cc.tint = 0;
    cc.flags &= ~CC_INVERTED;
    cc.space = COLOUR_SPACE_NORMAL;
    cc.flags |= CC_CCM_ENABLED;

    return cc;
}

Argb colour_correct(const ColourCorrection *cc, Argb c)
{
    if ((c & COLOUR_MASK) == COLOUR_MASK && !(cc->flags & CC_INVERTED) && (cc->flags & CC_CCM_ENABLED))
        return 0xFFFFEA;
    float r = (c >> 16) & COLOUR_CHANNEL_MASK;
    float g = (c >> 8) & COLOUR_CHANNEL_MASK;
    float b = (c >> 0) & COLOUR_CHANNEL_MASK;

    if (cc->flags & CC_INVERTED) {
	r = COLOUR_CHANNEL_MAX_F - r;
	g = COLOUR_CHANNEL_MAX_F - g;
	b = COLOUR_CHANNEL_MAX_F - b;
    }
    if (cc->contrast != 1.0f) {
	float cf = cc->contrast;
	r = (r - 128.0f) * cf + 128.0f;
	g = (g - 128.0f) * cf + 128.0f;
	b = (b - 128.0f) * cf + 128.0f;
    }
    if (cc->brightness != 1.0f) {
	r *= cc->brightness;
	g *= cc->brightness;
	b *= cc->brightness;
    }
    if (cc->saturation < 1.0f) {
	float lr = srgb_to_linear(r);
	float lg = srgb_to_linear(g);
	float lb = srgb_to_linear(b);
	float lu = luma(lr, lg, lb);
	float s = cc->saturation;
	lr = lu + s * (lr - lu);
	lg = lu + s * (lg - lu);
	lb = lu + s * (lb - lu);
	r = linear_to_srgb(lr);
	g = linear_to_srgb(lg);
	b = linear_to_srgb(lb);
    }
    if (cc->gamma != 1.0f) {
	float inv_gamma = 1.0f / cc->gamma;
	r = COLOUR_CHANNEL_MAX_F * powf(r / COLOUR_CHANNEL_MAX_F, inv_gamma);
	g = COLOUR_CHANNEL_MAX_F * powf(g / COLOUR_CHANNEL_MAX_F, inv_gamma);
	b = COLOUR_CHANNEL_MAX_F * powf(b / COLOUR_CHANNEL_MAX_F, inv_gamma);
    }
    if (cc->tint) {
	float ta = ((cc->tint >> 24) & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX_F;
	Argb tc = cc->tint & 0x00FFFFFF;
	float tr = ((tc >> 16) & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX_F;
	float tg = ((tc >> 8) & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX_F;
	float tb = ((tc >> 0) & COLOUR_CHANNEL_MASK) / COLOUR_CHANNEL_MAX_F;
	r = r * (1.0f - ta) + tr * COLOUR_CHANNEL_MAX_F * ta;
	g = g * (1.0f - ta) + tg * COLOUR_CHANNEL_MAX_F * ta;
	b = b * (1.0f - ta) + tb * COLOUR_CHANNEL_MAX_F * ta;
    }
    if (cc->space == COLOUR_SPACE_SEPIA) {
	float nr = r * 0.393f + g * 0.769f + b * 0.189f;
	float ng = r * 0.349f + g * 0.686f + b * 0.168f;
	float nb = r * 0.272f + g * 0.534f + b * 0.131f;
	r = nr;
	g = ng;
	b = nb;
    } else if (cc->space == COLOUR_SPACE_GREYSCALE) {
	float l = r * 0.299f + g * 0.587f + b * 0.114f;
	r = g = b = l;
    }
    if (cc->flags & CC_CCM_ENABLED) {
	float lr = srgb_to_linear(r);
	float lg = srgb_to_linear(g);
	float lb = srgb_to_linear(b);
	float nr =
	    cc->ccm[0][0] * lr + cc->ccm[0][1] * lg + cc->ccm[0][2] * lb;
	float ng =
	    cc->ccm[1][0] * lr + cc->ccm[1][1] * lg + cc->ccm[1][2] * lb;
	float nb =
	    cc->ccm[2][0] * lr + cc->ccm[2][1] * lg + cc->ccm[2][2] * lb;
	r = linear_to_srgb(nr < 0.0f ? 0.0f : nr);
	g = linear_to_srgb(ng < 0.0f ? 0.0f : ng);
	b = linear_to_srgb(nb < 0.0f ? 0.0f : nb);
    }
    r = fminf(COLOUR_CHANNEL_MAX_F, fmaxf(0.0f, r));
    g = fminf(COLOUR_CHANNEL_MAX_F, fmaxf(0.0f, g));
    b = fminf(COLOUR_CHANNEL_MAX_F, fmaxf(0.0f, b));
    return ((uint32_t) (r + 0.5f) << 16) |
	((uint32_t) (g + 0.5f) << 8) | ((uint32_t) (b + 0.5f));
}

Argb colour_min_contrast(Argb fg, Argb bg, float min_ratio)
{
    float lfg = colour_luma(fg);
    float lbg = colour_luma(bg);
    float hi = lfg > lbg ? lfg : lbg;
    float lo = lfg > lbg ? lbg : lfg;

    if ((hi + 0.05f) / (lo + 0.05f) >= min_ratio)
	return fg;

    int lighten = lbg < 0.5f;
    float target = lighten
	? min_ratio * (lbg + 0.05f) - 0.05f
	: (lbg + 0.05f) / min_ratio - 0.05f;
    target = fminf(1.0f, fmaxf(0.0f, target));

    float lr = srgb_u8((fg >> 16) & COLOUR_CHANNEL_MASK);
    float lg = srgb_u8((fg >> 8) & COLOUR_CHANNEL_MASK);
    float lb = srgb_u8(fg & COLOUR_CHANNEL_MASK);
    apply_luminosity(&lr, &lg, &lb, target);
    lr = fminf(1.0f, fmaxf(0.0f, lr));
    lg = fminf(1.0f, fmaxf(0.0f, lg));
    lb = fminf(1.0f, fmaxf(0.0f, lb));

    if (lighten) {
	float got = luma(lr, lg, lb);
	if (got < target && got < 1.0f) {
	    float t = (target - got) / (1.0f - got);
	    lr += t * (1.0f - lr);
	    lg += t * (1.0f - lg);
	    lb += t * (1.0f - lb);
	}
    }
    return ((uint32_t) (linear_to_srgb(lr) + 0.5f) << 16) |
	((uint32_t) (linear_to_srgb(lg) + 0.5f) << 8) |
	((uint32_t) (linear_to_srgb(lb) + 0.5f));
}

bool colour_parse(const char *s, Argb *out)
{
    if (!s || !*s)
	return false;
    if (s[0] == '#') {
	unsigned long v = strtoul(s + 1, NULL, 16);
	if (strlen(s + 1) == 6) {
	    *out = (Argb) ((v >> 16) & COLOUR_CHANNEL_MASK) << 16 |
		(Argb) ((v >> 8) & COLOUR_CHANNEL_MASK) << 8 | (Argb) ((v) & COLOUR_CHANNEL_MASK);
	    return true;
	}
	if (strlen(s + 1) == 3) {
	    *out = (Argb) ((v >> 8) & 0xF) * 17 << 16 |
		(Argb) ((v >> 4) & 0xF) * 17 << 8 | (Argb) ((v) & 0xF) *
		17;
	    return true;
	}
	return false;
    }
    if (strncasecmp(s, "rgb:", 4) == 0) {
	unsigned int r = 0, g = 0, b = 0;
	if (sscanf(s + 4, "%02x/%02x/%02x", &r, &g, &b) == 3 ||
	    sscanf(s + 4, "%u/%u/%u", &r, &g, &b) == 3) {
	    *out = ((Argb) r << 16) | ((Argb) g << 8) | (Argb) b;
	    return true;
	}
	return false;
    }
    static const struct {
	const char *n;
	uint32_t v;
    } xterm_names[] = {
	{ "red3", 0xCD0000 }, { "green3", 0x00CD00 }, { "yellow3",
						       0xCDCD00 },
	{ "blue2", 0x0000EE }, { "magenta3", 0xCD00CD }, { "cyan3",
							  0x00CDCD },
	{ "grey92", 0xE5E5E5 }, { "grey50", 0x7F7F7F },
	{ "green", 0x00FF00 }, { "brightgreen", 0x00FF00 },
	{ "brightred", 0xFF0000 },
	{ "brightyellow", 0xFFFF00 }, { "brightblue", 0x5C5CFF },
	{ "brightmagenta", 0xFF00FF }, { "brightcyan", 0x00FFFF },
	{ "brightwhite", COLOUR_MASK }, { "brightblack", 0x7F7F7F },
    };
    for (size_t i = 0; i < sizeof(xterm_names) / sizeof(xterm_names[0]);
	 i++) {
	if (strcasecmp(s, xterm_names[i].n) == 0) {
	    *out = xterm_names[i].v;
	    return true;
	}
    }
    if (colour_parse_css(s, out))
	return true;
    return false;
}


static const struct {
    const char *name;
    Argb value;
} x11_colours[] = {
    { "aliceblue", 0xF0F8FF }, { "antiquewhite", 0xFAEBD7 }, { "aqua",
							      0x00FFFF },
    { "aquamarine", 0x7FFFD4 }, { "azure", 0xF0FFFF }, { "beige",
							0xF5F5DC },
    { "bisque", 0xFFE4C4 }, { "black", 0x000000 }, { "blanchedalmond",
						    0xFFEBCD },
    { "blue", 0x0000FF }, { "blueviolet", 0x8A2BE2 }, { "brown",
						       0xA52A2A },
    { "burlywood", 0xDEB887 }, { "cadetblue", 0x5F9EA0 }, { "chartreuse",
							   0x7FFF00 },
    { "chocolate", 0xD2691E }, { "coral", 0xFF7F50 }, { "cornflowerblue",
						       0x6495ED },
    { "cornsilk", 0xFFF8DC }, { "crimson", 0xDC143C }, { "cyan",
							0x00FFFF },
    { "darkblue", 0x00008B }, { "darkcyan", 0x008B8B }, { "darkgoldenrod",
							 0xB8860B },
    { "darkgrey", 0xA9A9A9 }, { "darkgreen", 0x006400 }, { "darkkhaki",
							  0xBDB76B },
    { "darkmagenta", 0x8B008B }, { "darkolivegreen", 0x556B2F },
    { "darkorange",
     0xFF8C00 },
    { "darkorchid", 0x9932CC }, { "darkred", 0x8B0000 }, { "darksalmon",
							  0xE9967A },
    { "darkseagreen", 0x8FBC8F }, { "darkslateblue", 0x483D8B },
    { "darkslategrey",
     0x2F4F4F },
    { "darkturquoise", 0x00CED1 }, { "darkviolet", 0x9400D3 },
    { "deeppink",
     0xFF1493 },
    { "deepskyblue", 0x00BFFF }, { "dimgrey", 0x696969 }, { "dodgerblue",
							   0x1E90FF },
    { "firebrick", 0xB22222 }, { "floralwhite", 0xFFFAF0 },
    { "forestgreen",
     0x228B22 },
    { "fuchsia", 0xFF00FF }, { "gainsboro", 0xDCDCDC }, { "ghostwhite",
							 0xF8F8FF },
    { "gold", 0xFFD700 }, { "goldenrod", 0xDAA520 }, { "grey", 0x808080 },
    { "green", 0x008000 }, { "greenyellow", 0xADFF2F }, { "honeydew",
							 0xF0FFF0 },
    { "hotpink", 0xFF69B4 }, { "indianred", 0xCD5C5C }, { "indigo",
							 0x4B0082 },
    { "ivory", 0xFFFFF0 }, { "khaki", 0xF0E68C }, { "lavender", 0xE6E6FA },
    { "lavenderblush", 0xFFF0F5 }, { "lawngreen", 0x7CFC00 },
    { "lemonchiffon",
     0xFFFACD },
    { "lightblue", 0xADD8E6 }, { "lightcoral", 0xF08080 }, { "lightcyan",
							    0xE0FFFF },
    { "lightgoldenrodyellow", 0xFAFAD2 }, { "lightgrey", 0xD3D3D3 },
    { "lightgreen",
     0x90EE90 },
    { "lightpink", 0xFFB6C1 }, { "lightsalmon", 0xFFA07A },
    { "lightseagreen",
     0x20B2AA },
    { "lightskyblue", 0x87CEFA }, { "lightslategrey", 0x778899 },
    { "lightsteelblue",
     0xB0C4DE },
    { "lightyellow", 0xFFFFE0 }, { "lime", 0x00FF00 }, { "limegreen",
							0x32CD32 },
    { "linen", 0xFAF0E6 }, { "magenta", 0xFF00FF }, { "maroon", 0x800000 },
    { "mediumaquamarine", 0x66CDAA }, { "mediumblue", 0x0000CD },
    { "mediumorchid",
     0xBA55D3 },
    { "mediumpurple", 0x9370DB }, { "mediumseagreen", 0x3CB371 },
    { "mediumslateblue",
     0x7B68EE },
    { "mediumspringgreen", 0x00FA9A }, { "mediumturquoise", 0x48D1CC },
    { "mediumvioletred", 0xC71585 },
    { "midnightblue", 0x191970 }, { "mintcream", 0xF5FFFA }, { "mistyrose",
							      0xFFE4E1 },
    { "moccasin", 0xFFE4B5 }, { "navajowhite", 0xFFDEAD }, { "navy",
							    0x000080 },
    { "oldlace", 0xFDF5E6 }, { "olive", 0x808000 }, { "olivedrab",
						     0x6B8E23 },
    { "orange", 0xFFA500 }, { "orangered", 0xFF4500 }, { "orchid",
							0xDA70D6 },
    { "palegoldenrod", 0xEEE8AA }, { "palegreen", 0x98FB98 },
    { "paleturquoise",
     0xAFEEEE },
    { "palevioletred", 0xDB7093 }, { "papayawhip", 0xFFEFD5 },
    { "peachpuff",
     0xFFDAB9 },
    { "peru", 0xCD853F }, { "pink", 0xFFC0CB }, { "plum", 0xDDA0DD },
    { "powderblue", 0xB0E0E6 }, { "purple", 0x800080 }, { "red",
							 0xFF0000 },
    { "rosybrown", 0xBC8F8F }, { "royalblue", 0x4169E1 }, { "saddlebrown",
							   0x8B4513 },
    { "salmon", 0xFA8072 }, { "sandybrown", 0xF4A460 }, { "seagreen",
							 0x2E8B57 },
    { "seashell", 0xFFF5EE }, { "sienna", 0xA0522D }, { "silver",
						       0xC0C0C0 },
    { "skyblue", 0x87CEEB }, { "slateblue", 0x6A5ACD }, { "slategrey",
							 0x708090 },
    { "snow", 0xFFFAFA }, { "springgreen", 0x00FF7F }, { "steelblue",
							0x4682B4 },
    { "tan", 0xD2B48C }, { "teal", 0x008080 }, { "thistle", 0xD8BFD8 },
    { "tomato", 0xFF6347 }, { "turquoise", 0x40E0D0 }, { "violet",
							0xEE82EE },
    { "wheat", 0xF5DEB3 }, { "white", COLOUR_MASK }, { "whitesmoke",
						   0xF5F5F5 },
    { "yellow", 0xFFFF00 }, { "yellowgreen", 0x9ACD32 },
};
#define X11_COLOUR_COUNT (sizeof(x11_colours) / sizeof(x11_colours[0]))

bool colour_parse_css(const char *name, Argb *out)
{
    size_t i;
    for (i = 0; i < X11_COLOUR_COUNT; i++) {
	if (strcasecmp(name, x11_colours[i].name) == 0) {
	    *out = x11_colours[i].value;
	    return true;
	}
    }
    return false;
}

Argb
palette_resolve_corrected(const Palette *p, int idx,
			  const ColourCorrection *cc)
{
    Argb raw = (Argb) idx;
    if (IS_TRUECOLOUR(raw))
	return colour_correct(cc, palette_resolve(p, raw));
    if (BETWEEN(idx, 0, PAL_SIZE - 1))
	return colour_correct(cc, palette_resolve(p, raw));
    return 0;
}

Argb colour_random_lum(float target_lum)
{
    float lr = srgb_to_linear(rand() % 256);
    float lg = srgb_to_linear(rand() % 256);
    float lb = srgb_to_linear(rand() % 256);
    apply_luminosity(&lr, &lg, &lb, target_lum);
    lr = fminf(1.0f, fmaxf(0.0f, lr));
    lg = fminf(1.0f, fmaxf(0.0f, lg));
    lb = fminf(1.0f, fmaxf(0.0f, lb));
    return ((uint32_t) (linear_to_srgb(lr) + 0.5f) << 16) |
	((uint32_t) (linear_to_srgb(lg) + 0.5f) << 8) |
	((uint32_t) (linear_to_srgb(lb) + 0.5f));
}

void palette_randomise(Palette *p)
{
    float fg_lum = 0.08f + (rand() % 501) / 2500.0f;
    Argb fg = colour_random_lum(fg_lum);
    float bg_lum = 0.80f + (rand() % 301) / 3000.0f;
    float lr = srgb_to_linear((fg >> 16) & COLOUR_CHANNEL_MASK);
    float lg = srgb_to_linear((fg >> 8) & COLOUR_CHANNEL_MASK);
    float lb = srgb_to_linear(fg & COLOUR_CHANNEL_MASK);
    apply_luminosity(&lr, &lg, &lb, bg_lum);
    lr = fminf(1.0f, fmaxf(0.0f, lr));
    lg = fminf(1.0f, fmaxf(0.0f, lg));
    lb = fminf(1.0f, fmaxf(0.0f, lb));
    Argb bg = ((uint32_t) (linear_to_srgb(lr) + 0.5f) << 16) |
	((uint32_t) (linear_to_srgb(lg) + 0.5f) << 8) |
	((uint32_t) (linear_to_srgb(lb) + 0.5f));
    palette_load_fgbg(p, fg, bg, NULL, PAL_SIZE);
}

void palette_flip_fg_bg(Palette *p)
{
    Argb fg = palette_get(p, PAL_DEFAULT_FG);
    Argb bg = palette_get(p, PAL_DEFAULT_BG);
    palette_load_fgbg(p, bg, fg, NULL, PAL_SIZE);
}
