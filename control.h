#ifndef CONTROL_H
#define CONTROL_H
#include "parser.h"
#include "term.h"


enum {
    CS_DEC_GRAPHICS,
    CS_USA
};

void control_dispatch_batch(Term * t, const Event * ev, int nev);
#endif
