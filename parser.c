#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "utf8.h"

static void parser_reset(Parser *p);



typedef enum {
    PS_GROUND,
    PS_ESC,
    PS_CSI,
    PS_OSC,
    PS_DCS,
    PS_APC,
    PS_PM,
    PS_SOS,
    PS_ESC_SEQ,
} PState;

struct Parser {
    PState state;


    CsiEvent csi;


    uint8_t esc_prefix;


    char strbuf[PARSER_STR_BUF];
    size_t strpos;
    int strtype;
};

static void reset_csi(Parser *p)
{
    p->csi.priv = 0;
    p->csi.narg = 0;
    p->csi.nfinal = 0;
    memset(p->csi.arg, 0, sizeof(p->csi.arg));
    memset(p->csi.final, 0, sizeof(p->csi.final));
}

static void reset_str(Parser *p)
{
    p->strpos = 0;
    p->strtype = -1;
    p->strbuf[0] = '\0';
}

Parser *parser_new(void)
{
    Parser *p = calloc(1, sizeof(*p));
    if (p)
	parser_reset(p);
    return p;
}

void parser_free(Parser *p)
{
    free(p);
}

static void parser_reset(Parser *p)
{
    p->state = PS_GROUND;
    reset_csi(p);
    reset_str(p);
}



static int push_narg(CsiEvent *csi, int val)
{
    if (csi->narg >= PARSER_CSI_ARGS)
	return -1;
    csi->arg[csi->narg++] = val;
    return 0;
}


static void on_ground_char(Parser *p, uint8_t c, Event *ev, int *n,
			   int cap)
{
    if (c == 0x1B) {
	p->state = PS_ESC;
	return;
    }

    if (c < 0x20 || c == 0x7F) {
	if (*n < cap) {
	    ev[*n].type = EVENT_CONTROL;
	    ev[*n].data.code = c;
	    (*n)++;
	}
	return;
    }


    if (c >= 0x80 && c <= 0x9F) {
	enum { C1_ESC, C1_STR, C1_CSI };
	static const uint8_t c1_type[32] = {
	    [0x04] = C1_ESC,[0x05] = C1_ESC,[0x08] = C1_ESC,
	    [0x0D] = C1_ESC,[0x10] = C1_STR,[0x1A] = C1_ESC,
	    [0x1B] = C1_CSI,[0x1D] = C1_STR,[0x1E] = C1_STR,
	    [0x1F] = C1_STR,
	};
	static const uint8_t c1_val[32] = {
	    [0x04] = 'D',[0x05] = 'E',[0x08] = 'H',
	    [0x0D] = 'M',[0x10] = PS_DCS,[0x1A] = 'Z',
	    [0x1B] = PS_CSI,[0x1D] = PS_OSC,[0x1E] = PS_PM,
	    [0x1F] = PS_APC,
	};
	uint8_t t = c1_type[c - 0x80];
	uint8_t v = c1_val[c - 0x80];
	if (t == C1_ESC && v != 0) {
	    if (*n < cap) {
		ev[*n].type = EVENT_ESC;
		ev[*n].data.esc.prefix = 0;
		ev[*n].data.esc.b = v;
		(*n)++;
	    }
	} else if (t == C1_STR) {
	    p->state = v;
	    reset_str(p);
	} else if (t == C1_CSI) {
	    p->state = v;
	    reset_csi(p);
	}
	return;
    }


    if (*n < cap) {
	ev[*n].type = EVENT_PRINT;
	ev[*n].data.r = c;
	(*n)++;
    }
}



static void on_esc(Parser *p, uint8_t c, Event *ev, int *n, int cap)
{
    switch (c) {
    case '[':
	p->state = PS_CSI;
	reset_csi(p);
	return;
    case ']':
	p->state = PS_OSC;
	reset_str(p);
	return;
    case 'P':
	p->state = PS_DCS;
	reset_str(p);
	return;
    case '_':
	p->state = PS_APC;
	reset_str(p);
	return;
    case '^':
	p->state = PS_PM;
	reset_str(p);
	return;
    case 'X':
	p->state = PS_SOS;
	reset_str(p);
	return;
    case 'k':
	p->state = PS_OSC;
	reset_str(p);
	p->strtype = 'k';
	return;
    case '\\':
	p->state = PS_GROUND;
	return;
    case '#':
    case '%':
    case ' ':
    case '(':
    case ')':
    case '*':
    case '+':
	p->esc_prefix = c;
	p->state = PS_ESC_SEQ;
	return;
    default:
	if (*n < cap) {
	    ev[*n].type = EVENT_ESC;
	    ev[*n].data.esc.prefix = 0;
	    ev[*n].data.esc.b = c;
	    (*n)++;
	}
	p->state = PS_GROUND;
	return;
    }
}



static int csi_collect(CsiEvent *csi, uint8_t c)
{

    if (BETWEEN(c, 0x30, 0x3F)) {
	if (c == ';') {
	    push_narg(csi, 0);
	} else if (c == ':') {
	    push_narg(csi, -1);
	} else if (c == '?' || c == '>' || c == '\'' || c == '"'
		   || c == '!') {
	    if (csi->narg == 0 && csi->nfinal == 0)
		csi->priv = c;
	} else {
	    int v = c - '0';
	    if (csi->narg == 0) {
		push_narg(csi, v);
	    } else if (csi->narg > 0 && csi->narg <= PARSER_CSI_ARGS) {
		int *last = &csi->arg[csi->narg - 1];
		if (*last >= 0 && *last < 100000)
		    *last = *last * 10 + v;
	    }
	}
	return 0;
    }


    if (BETWEEN(c, 0x20, 0x2F)) {
	if (csi->nfinal < 2)
	    csi->final[csi->nfinal++] = c;
	return 0;
    }


    if (BETWEEN(c, 0x40, 0x7E)) {
	if (csi->nfinal < 3)
	    csi->final[csi->nfinal++] = c;
	if (csi->narg == 0)
	    push_narg(csi, 0);
	return 1;
    }

    return 0;
}



static void emit_str(Parser *p, EventType type, Event *ev, int *n, int cap)
{
    if (*n >= cap)
	return;
    char *buf = malloc(p->strpos + 1);
    if (!buf)
	return;
    p->strbuf[p->strpos] = '\0';
    memcpy(buf, p->strbuf, p->strpos + 1);
    ev[*n].type = type;
    ev[*n].data.str.par = p->strtype;
    ev[*n].data.str.buf = buf;
    (*n)++;
}

static const EventType str_evtype[] = {
    [PS_OSC] = EVENT_OSC,
    [PS_DCS] = EVENT_DCS,
    [PS_APC] = EVENT_APC,
    [PS_PM] = EVENT_PM,
    [PS_SOS] = EVENT_SOS,
};

static void on_str(Parser *p, uint8_t c, Event *ev, int *n, int cap)
{
    EventType type = str_evtype[p->state];
    if (c == 0x1B) {
	emit_str(p, type, ev, n, cap);
	p->state = PS_ESC;
	return;
    }
    if (c == 0x07) {
	emit_str(p, type, ev, n, cap);
	p->state = PS_GROUND;
	reset_str(p);
	return;
    }
    if (p->strpos < PARSER_STR_BUF - 1)
	p->strbuf[p->strpos++] = c;
}

typedef void (*state_fn)(Parser *, uint8_t, Event *, int *, int);

static void on_csi(Parser *p, uint8_t c, Event *ev, int *n, int cap)
{
    if (csi_collect(&p->csi, c)) {
	if (*n < cap) {
	    ev[*n].type = EVENT_CSI;
	    ev[*n].data.csi = p->csi;
	    (*n)++;
	}
	p->state = PS_GROUND;
	reset_csi(p);
    }
}

static void on_esc_seq(Parser *p, uint8_t c, Event *ev, int *n, int cap)
{
    if (*n < cap) {
	ev[*n].type = EVENT_ESC;
	ev[*n].data.esc.prefix = p->esc_prefix;
	ev[*n].data.esc.b = c;
	(*n)++;
    }
    p->state = PS_GROUND;
}

static const state_fn state_table[] = {
    [PS_ESC] = on_esc,
    [PS_CSI] = on_csi,
    [PS_OSC] = on_str,
    [PS_DCS] = on_str,
    [PS_APC] = on_str,
    [PS_PM] = on_str,
    [PS_SOS] = on_str,
    [PS_ESC_SEQ] = on_esc_seq,
};

int parser_feed(Parser *p, const char *buf, size_t len, Event *ev, int cap)
{
    int n = 0;
    size_t i = 0;

    while (i < len) {
	uint8_t c = (uint8_t) buf[i];

	if (p->state == PS_GROUND) {
	    if (c >= 0x20 && c <= 0x7E) {
		do {
		    if (n < cap) {
			ev[n].type = EVENT_PRINT;
			ev[n].data.r = c;
			n++;
		    }
		    i++;
		}
		while (i < len && (c = (uint8_t) buf[i]) >= 0x20
		       && c <= 0x7E);
		continue;
	    }

	    if (c >= 0xC0 && c <= 0xDF) {
		if (i + 1 < len) {
		    uint8_t t = (uint8_t) buf[i + 1];
		    if ((t & 0xC0) == 0x80) {
			Rune r = ((Rune) (c & 0x1F) << 6) | (t & 0x3F);
			if (r >= 0x80) {
			    if (n < cap) {
				ev[n].type = EVENT_PRINT;
				ev[n].data.r = r;
				n++;
			    }
			    i += 2;
			    continue;
			}
		    }
		}
		i++;
		continue;
	    }

	    if (c >= 0xE0 && c <= 0xF4) {
		int needed = (c >= 0xF0) ? 4 : 3;
		if (i + (size_t) needed <= len) {
		    const char *cp = buf + i;
		    Rune r;
		    size_t dlen = utf8_decode(&cp, &r);
		    if (dlen > 0) {
			if (n < cap) {
			    ev[n].type = EVENT_PRINT;
			    ev[n].data.r = r;
			    n++;
			}
			i += dlen;
			continue;
		    }
		}

		i++;
		continue;
	    }

	    on_ground_char(p, c, ev, &n, cap);
	    i++;
	    continue;
	}


	if (c >= 0x80 && c < 0xC0) {
	    if (p->state < PS_OSC || p->state > PS_SOS) {
		i++;
		continue;
	    }
	}
	if (c > 0xF4) {
	    i++;
	    continue;
	}

	state_fn fn = state_table[p->state];
	fn(p, c, ev, &n, cap);
	i++;
    }

    return n;
}
