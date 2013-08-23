#ifndef PROC_H
#define PROC_H
#include <stddef.h>
#include "pty.h"


int proc_chain(Pty * p, char *out, size_t cap);
#endif
