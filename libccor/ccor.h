#ifndef CCOR_H
#define CCOR_H
#include <stdbool.h>
#include <stdint.h>

#define PAL_SIZE 259
#define PAL_DEFAULT_FG 256
#define PAL_DEFAULT_BG 257
#define PAL_DEFAULT_CS 258
#define TRUECOLOUR_BIT 0x80000000U
#define IS_TRUECOLOUR(c) ((c) & TRUECOLOUR_BIT)
#define TRUECOLOUR(r,g,b) (TRUECOLOUR_BIT | ((r) << 16) | ((g) << 8) | (b))
#define TRUERED(c)   (((c) & 0x00FF0000) >> 16)
#define TRUEGREEN(c) (((c) & 0x0000FF00) >> 8)
#define TRUEBLUE(c)  (((c) & 0x000000FF))

typedef uint32_t Argb;

#define COLOUR_CHANNEL_MASK  0xFF
#define COLOUR_CHANNEL_MAX   255
#define COLOUR_CHANNEL_MAX_F 255.0f
#define COLOUR_MASK          0xFFFFFF

typedef enum {
    COLOUR_SPACE_NORMAL,
    COLOUR_SPACE_SEPIA,
    COLOUR_SPACE_GREYSCALE,
} ColourSpace;

typedef struct {
    float gamma;
    float brightness;
    float contrast;
    Argb tint;
    float saturation;
    ColourSpace space;
    float ccm[3][3];
    unsigned flags;
} ColourCorrection;

enum {
    CC_INVERTED = 1 << 0,
    CC_CCM_ENABLED = 1 << 1,
};

typedef struct Palette Palette;

extern const ColourCorrection COLOUR_CORRECTION_DEFAULT;

Palette *palette_new(void);
void palette_free(Palette * p);
void palette_load(Palette * p, const char *colourname[], int ncolours,
		  Argb default_fg, Argb default_bg);
void palette_load_fgbg(Palette * p, Argb fg, Argb bg,
		       const char *colourname[], int ncolours);
void palette_reload(Palette * p);
void palette_set(Palette * p, int idx, Argb colour);
Argb palette_get(const Palette * p, int idx);
unsigned palette_generation(const Palette * p);
void palette_reset(Palette * p, int idx);
Argb palette_resolve(const Palette * p, Argb raw);
Argb palette_resolve_corrected(const Palette * p, int idx,
			       const ColourCorrection * cc);

Argb colour_correct(const ColourCorrection * cc, Argb c);
Argb colour_min_contrast(Argb fg, Argb bg, float min_ratio);
bool colour_parse(const char *s, Argb * out);
bool colour_parse_css(const char *name, Argb * out);
ColourCorrection colour_regression_ccm(const Palette * p);
float colour_luma(Argb c);
float srgb_to_linear(float c);
float linear_to_srgb(float c);
void apply_luminosity(float *lr, float *lg, float *lb, float target_lum);
Argb colour_random_lum(float target_lum);
void palette_randomise(Palette * p);
void palette_flip_fg_bg(Palette * p);
#endif
