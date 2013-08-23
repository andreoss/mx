#include "utf8.h"

#define UTF8_CONT     0x80
#define UTF8_LEAD     0xC0
#define UTF8_MASK     0x3F

#define UTF8_1B_DATA  0x7F
#define UTF8_2B_DATA  0x1F
#define UTF8_3B_DATA  0x0F
#define UTF8_4B_DATA  0x07

#define UTF8_2B_LEAD  0xC0
#define UTF8_3B_LEAD  0xE0
#define UTF8_4B_LEAD  0xF0
#define UTF8_4B_MAX   0xF4

#define UTF8_RANGE_2B_MIN   0x80
#define UTF8_RANGE_3B_MIN   0x800
#define UTF8_RANGE_4B_MIN   0x10000
#define UTF8_RANGE_MAX      0x10FFFF

#define UTF8_REPLACEMENT_CHAR  0xFFFD
#define UTF8_SUR_HI   0xD800
#define UTF8_SUR_LO   0xDFFF


static Rune utf8_validate(Rune r);


static size_t utf8_seqlen(uint8_t c)
{
    if (c < UTF8_RANGE_2B_MIN)
	return 1;
    if (c < UTF8_2B_LEAD)
	return 0;
    if (c < UTF8_3B_LEAD)
	return 2;
    if (c < UTF8_4B_LEAD)
	return 3;
    if (c <= UTF8_4B_MAX)
	return 4;
    return 0;
}

static const struct {
    Rune mask;
    Rune minv;
} tab[] = {
    { UTF8_1B_DATA, 0 },
    { UTF8_2B_DATA, UTF8_RANGE_2B_MIN },
    { UTF8_3B_DATA, UTF8_RANGE_3B_MIN },
    { UTF8_4B_DATA, UTF8_RANGE_4B_MIN },
};

size_t utf8_decode(const char **src, Rune *r)
{
    const uint8_t *s = (const uint8_t *) *src;
    size_t len = utf8_seqlen(s[0]);
    Rune rune;
    uint8_t trail;
    size_t i;

    if (len == 0 || len > 4)
	return 0;

    rune = s[0] & tab[len - 1].mask;
    for (i = 1; i < len; i++) {
	trail = s[i];
	if ((trail & UTF8_LEAD) != UTF8_CONT)
	    return 0;
	rune = (rune << 6) | (trail & UTF8_MASK);
    }


    if (len > 1 && rune < tab[len - 1].minv)
	return 0;

    rune = utf8_validate(rune);
    *r = rune;
    *src += len;
    return len;
}

size_t utf8_encode(Rune r, char dst[UTF_SIZ])
{
    uint8_t *d = (uint8_t *) dst;

    if (r < UTF8_RANGE_2B_MIN) {
	d[0] = r;
	return 1;
    }
    if (r < UTF8_RANGE_3B_MIN) {
	d[0] = UTF8_2B_LEAD | (r >> 6);
	d[1] = UTF8_CONT | (r & UTF8_MASK);
	return 2;
    }
    if (r < UTF8_RANGE_4B_MIN) {
	d[0] = UTF8_3B_LEAD | (r >> 12);
	d[1] = UTF8_CONT | ((r >> 6) & UTF8_MASK);
	d[2] = UTF8_CONT | (r & UTF8_MASK);
	return 3;
    }
    if (r <= UTF8_RANGE_MAX) {
	d[0] = UTF8_4B_LEAD | (r >> 18);
	d[1] = UTF8_CONT | ((r >> 12) & UTF8_MASK);
	d[2] = UTF8_CONT | ((r >> 6) & UTF8_MASK);
	d[3] = UTF8_CONT | (r & UTF8_MASK);
	return 4;
    }

    return 0;
}

static Rune utf8_validate(Rune r)
{
    if (r > UTF8_RANGE_MAX)
	return UTF8_REPLACEMENT_CHAR;
    if (r >= UTF8_SUR_HI && r <= UTF8_SUR_LO)
	return UTF8_REPLACEMENT_CHAR;
    return r;
}
