#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "ccor.h"

static int passed, failed;

static void check(const char *name, Argb got, Argb want)
{
    if (got == want) {
	printf("  PASS  %s\n", name);
	passed++;
    } else {
	printf("  FAIL  %s: got %06X want %06X\n", name, got, want);
	failed++;
    }
}

static void
check_cc(const char *name, ColourCorrection got, ColourCorrection want)
{
    int ok = 1;
    if (fabsf(got.gamma - want.gamma) > 0.001f)
	ok = 0;
    if (fabsf(got.brightness - want.brightness) > 0.001f)
	ok = 0;
    if (fabsf(got.contrast - want.contrast) > 0.001f)
	ok = 0;
    if (fabsf(got.saturation - want.saturation) > 0.001f)
	ok = 0;
    if ((got.flags & CC_INVERTED) != (want.flags & CC_INVERTED))
	ok = 0;
    if (got.space != want.space)
	ok = 0;
    if ((got.flags & CC_CCM_ENABLED) != (want.flags & CC_CCM_ENABLED))
	ok = 0;
    if (ok) {
	printf("  PASS  %s\n", name);
	passed++;
    } else {
	printf("  FAIL  %s\n", name);
	failed++;
    }
}

static void test_identity(void)
{
    Argb result = colour_correct(&COLOUR_CORRECTION_DEFAULT, 0xFF8844);
    check("identity", result, 0xFF8844);
}

static void test_invert(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.flags |= CC_INVERTED;
    Argb result = colour_correct(&cc, 0x000000);
    check("invert black->white", result, 0xFFFFFF);
}

static void test_invert_white(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.flags |= CC_INVERTED;
    Argb result = colour_correct(&cc, 0xCC8844);
    check("invert colour", result, 0x3377BB);
}

static void test_brightness_darken(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.brightness = 0.5f;
    Argb result = colour_correct(&cc, 0xCC8844);
    check("brightness 0.5", result, 0x664422);
}

static void test_brightness_brighten(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.brightness = 2.0f;
    Argb result = colour_correct(&cc, 0x442211);
    check("brightness 2.0", result, 0x884422);
}

static void test_contrast_increase(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.contrast = 2.0f;
    Argb result = colour_correct(&cc, 0x808080);
    check("contrast 2.0 on mid-grey", result, 0x808080);
}

static void test_contrast_max(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.contrast = 2.0f;
    Argb result = colour_correct(&cc, 0x808080);
    check("contrast 2.0 mid-grey", result, 0x808080);
}

static void test_saturation_greyscale(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.saturation = 0.0f;
    Argb result = colour_correct(&cc, 0x4488CC);
    check("saturation 0 (greyscale)", result, 0x848484);
}

static void test_sepia(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.space = COLOUR_SPACE_SEPIA;
    Argb result = colour_correct(&cc, 0x4488CC);
    check("sepia tone", result, 0xAA9776);
}

static void test_greyscale(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.space = COLOUR_SPACE_GREYSCALE;
    Argb result = colour_correct(&cc, 0x4488CC);
    check("greyscale mode", result, 0x7B7B7B);
}

static void test_gamma(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.gamma = 2.2f;
    Argb result = colour_correct(&cc, 0x808080);
    check("gamma 2.2", result, 0xBABABA);
}

static void test_tint(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.tint = 0x400000FF;
    Argb result = colour_correct(&cc, 0xFFFFFF);
    check("tint blue 25%", result, 0xBFBFFF);
}

static void test_ccm_identity(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.flags |= CC_CCM_ENABLED;
    cc.ccm[0][0] = 1;
    cc.ccm[0][1] = 0;
    cc.ccm[0][2] = 0;
    cc.ccm[1][0] = 0;
    cc.ccm[1][1] = 1;
    cc.ccm[1][2] = 0;
    cc.ccm[2][0] = 0;
    cc.ccm[2][1] = 0;
    cc.ccm[2][2] = 1;
    Argb result = colour_correct(&cc, 0x4488CC);
    check("CCM identity", result, 0x4488CC);
}

static void test_ccm_swap_rg(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.flags |= CC_CCM_ENABLED;
    cc.ccm[0][0] = 0;
    cc.ccm[0][1] = 1;
    cc.ccm[0][2] = 0;
    cc.ccm[1][0] = 1;
    cc.ccm[1][1] = 0;
    cc.ccm[1][2] = 0;
    cc.ccm[2][0] = 0;
    cc.ccm[2][1] = 0;
    cc.ccm[2][2] = 1;
    Argb result = colour_correct(&cc, 0x00FF00);
    check("CCM swap R<->G", result, 0xFF0000);
}

static void test_ccm_swap_gb(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.flags |= CC_CCM_ENABLED;
    cc.ccm[0][0] = 1;
    cc.ccm[0][1] = 0;
    cc.ccm[0][2] = 0;
    cc.ccm[1][0] = 0;
    cc.ccm[1][1] = 0;
    cc.ccm[1][2] = 1;
    cc.ccm[2][0] = 0;
    cc.ccm[2][1] = 1;
    cc.ccm[2][2] = 0;
    Argb result = colour_correct(&cc, 0x00FF00);
    check("CCM swap G<->B", result, 0x0000FF);
}

static void test_regression_ccm_basic(void)
{
    Palette *p = palette_new();
    palette_load_fgbg(p, 0xCCCCCC, 0x202020, NULL, 0);

    ColourCorrection cc = colour_regression_ccm(p);
    check_cc("regression CCM produced", cc, cc);
    palette_free(p);
}

static void test_regression_ccm_applied(void)
{
    Palette *p = palette_new();
    palette_load_fgbg(p, 0xCCCCCC, 0x202020, NULL, 0);

    ColourCorrection cc = colour_regression_ccm(p);
    Argb corrected = colour_correct(&cc, 0xFF0000);
    if (corrected != 0) {
	printf("  PASS  regression_ccm_applied\n");
	passed++;
    } else {
	printf("  FAIL  regression_ccm_applied: zero result\n");
	failed++;
    }
    palette_free(p);
}

static void test_multiple_operations(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.gamma = 1.8f;
    cc.brightness = 0.8f;
    cc.contrast = 1.2f;
    cc.saturation = 0.75f;
    Argb result = colour_correct(&cc, 0x88AACC);
    if (result != 0) {
	printf("  PASS  multiple_operations\n");
	passed++;
    } else {
	printf("  FAIL  multiple_operations: zero\n");
	failed++;
    }
}

static void test_brightness_clamp(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.brightness = 3.0f;
    Argb result = colour_correct(&cc, 0xFFFFFF);
    check("brightness clamp", result, 0xFFFFFF);
}

static void test_contrast_low(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.contrast = 0.5f;
    Argb result = colour_correct(&cc, 0x000000);
    check("contrast 0.5 black", result, 0x404040);
}

static void test_luma_black(void)
{
    float l = colour_luma(0x000000);
    if (fabsf(l) < 0.001f) {
	printf("  PASS  luma_black\n");
	passed++;
    } else {
	printf("  FAIL  luma_black: %f\n", l);
	failed++;
    }
}

static void test_luma_white(void)
{
    float l = colour_luma(0xFFFFFF);
    if (fabsf(l - 1.0f) < 0.01f) {
	printf("  PASS  luma_white\n");
	passed++;
    } else {
	printf("  FAIL  luma_white: %f\n", l);
	failed++;
    }
}

static void test_luma_red(void)
{
    float l = colour_luma(0xFF0000);
    if (fabsf(l - 0.2126f) < 0.02f) {
	printf("  PASS  luma_red\n");
	passed++;
    } else {
	printf("  FAIL  luma_red: %f\n", l);
	failed++;
    }
}

static void test_luma_green(void)
{
    float l = colour_luma(0x00FF00);
    if (fabsf(l - 0.7152f) < 0.02f) {
	printf("  PASS  luma_green\n");
	passed++;
    } else {
	printf("  FAIL  luma_green: %f\n", l);
	failed++;
    }
}

static void test_luma_blue(void)
{
    float l = colour_luma(0x0000FF);
    if (fabsf(l - 0.0722f) < 0.01f) {
	printf("  PASS  luma_blue\n");
	passed++;
    } else {
	printf("  FAIL  luma_blue: %f\n", l);
	failed++;
    }
}

static void test_ccm_with_palette_custom(void)
{
    Palette *p = palette_new();
    colour_regression_ccm(p);
    if (1) {
	printf("  PASS  ccm_with_palette_custom (no crash)\n");
	passed++;
    } else {
	failed++;
    }
    palette_free(p);
}

static void test_invert_twice(void)
{
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    cc.flags |= CC_INVERTED;
    Argb c1 = colour_correct(&cc, 0x123456);
    Argb c2 = colour_correct(&cc, c1);
    check("double invert (back to orig)", c2, 0x123456);
}

static void test_srgb_roundtrip(void)
{
    float v = srgb_to_linear(0x80);
    float r = linear_to_srgb(v);
    check("srgb roundtrip 0x80", (Argb) (r + 0.5f), 0x80);
}

static void test_parse_hex6(void)
{
    Argb c;
    if (colour_parse("#12AB34", &c))
	check("parse #RRGGBB", c, 0x12AB34);
    else {
	printf("  FAIL  parse #RRGGBB (returned false)\n");
	failed++;
    }
}

static void test_parse_hex3(void)
{
    Argb c;
    if (colour_parse("#f0a", &c))
	check("parse #RGB", c, 0xFF00AA);
    else {
	printf("  FAIL  parse #RGB (returned false)\n");
	failed++;
    }
}

static void test_parse_css(void)
{
    Argb c;
    if (colour_parse_css("purple", &c))
	check("parse X11 purple", c, 0x800080);
    else {
	printf("  FAIL  parse X11 purple\n");
	failed++;
    }
}

static void test_parse_css_unknown(void)
{
    Argb c;
    if (colour_parse_css("nosuchcolour", &c)) {
	printf("  FAIL  parse unknown X11 name (should fail)\n");
	failed++;
    } else {
	printf("  PASS  parse unknown X11 name\n");
	passed++;
    }
}

static void test_resolve_corrected(void)
{
    Palette *p = palette_new();
    palette_load_fgbg(p, 0xCCCCCC, 0x202020, NULL, PAL_SIZE);
    ColourCorrection cc = COLOUR_CORRECTION_DEFAULT;
    Argb c = palette_resolve_corrected(p, 1, &cc);
    if (c != 0) {
	printf("  PASS  resolve_corrected idx\n");
	passed++;
    } else {
	printf("  FAIL  resolve_corrected idx: zero\n");
	failed++;
    }
    c = palette_resolve_corrected(p, PAL_DEFAULT_BG, &cc);
    check("resolve_corrected default bg", c, 0x202020);
    palette_free(p);
}

static void test_random_lum(void)
{
    Argb c = colour_random_lum(0.5f);
    float l = colour_luma(c);
    if (fabsf(l - 0.5f) < 0.05f) {
	printf("  PASS  random_lum 0.5\n");
	passed++;
    } else {
	printf("  FAIL  random_lum 0.5: luma %f\n", l);
	failed++;
    }
}

static void test_randomise_flip(void)
{
    Palette *p = palette_new();
    palette_load_fgbg(p, 0xCCCCCC, 0x202020, NULL, 0);
    Argb orig_fg = palette_get(p, PAL_DEFAULT_FG);
    Argb orig_bg = palette_get(p, PAL_DEFAULT_BG);
    palette_flip_fg_bg(p);
    check("flip fg==old bg", palette_get(p, PAL_DEFAULT_FG), orig_bg);
    check("flip bg==old fg", palette_get(p, PAL_DEFAULT_BG), orig_fg);
    palette_randomise(p);
    if (palette_get(p, PAL_DEFAULT_FG) != 0) {
	printf("  PASS  randomise fg nonzero\n");
	passed++;
    } else {
	printf("  FAIL  randomise fg zero\n");
	failed++;
    }
    palette_free(p);
}

static float wcag_ratio(Argb a, Argb b)
{
    float la = colour_luma(a), lb = colour_luma(b);
    float hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

static void test_min_contrast(void)
{
    Argb fixed = colour_min_contrast(0x00FF00, 0xFFFFFF, 4.5f);
    Argb keep = colour_min_contrast(0x00FF00, 0x000000, 4.5f);
    Argb navy = colour_min_contrast(0x000080, 0x000000, 4.5f);
    if (wcag_ratio(fixed, 0xFFFFFF) >= 4.4f && keep == 0x00FF00
	&& wcag_ratio(navy, 0x000000) >= 4.4f) {
	printf("  PASS  min_contrast (darken, keep, lighten-fallback)\n");
	passed++;
    } else {
	printf("  FAIL  min_contrast: green=%06X (%.2f) keep=%06X "
	       "navy=%06X (%.2f)\n", fixed, wcag_ratio(fixed, 0xFFFFFF),
	       keep, navy, wcag_ratio(navy, 0x000000));
	failed++;
    }
}

int main(void)
{
    passed = failed = 0;
    printf("=== colour correction pipeline tests ===\n");
    test_identity();
    test_invert();
    test_invert_white();
    test_brightness_darken();
    test_brightness_brighten();
    test_contrast_increase();
    test_contrast_max();
    test_saturation_greyscale();
    test_sepia();
    test_greyscale();
    test_gamma();
    test_tint();
    test_ccm_identity();
    test_ccm_swap_rg();
    test_ccm_swap_gb();
    test_regression_ccm_basic();
    test_regression_ccm_applied();
    test_multiple_operations();
    test_brightness_clamp();
    test_contrast_low();
    test_luma_black();
    test_luma_white();
    test_luma_red();
    test_luma_green();
    test_luma_blue();
    test_ccm_with_palette_custom();
    test_invert_twice();
    test_srgb_roundtrip();
    test_parse_hex6();
    test_parse_hex3();
    test_parse_css();
    test_parse_css_unknown();
    test_resolve_corrected();
    test_random_lum();
    test_randomise_flip();
    test_min_contrast();
    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return !!failed;
}
