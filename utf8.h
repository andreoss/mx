#ifndef UTF8_H
#define UTF8_H
#include "types.h"


size_t utf8_decode(const char **src, Rune * r);


size_t utf8_encode(Rune r, char dst[UTF_SIZ]);
#endif
