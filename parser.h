#ifndef PARSER_H
#define PARSER_H
#include "types.h"


#define PARSER_CSI_ARGS 16


#define PARSER_STR_BUF  4096


#define PARSER_STR_ARGS 16

typedef enum {
    EVENT_PRINT,
    EVENT_CONTROL,
    EVENT_CSI,
    EVENT_OSC,
    EVENT_ESC,
    EVENT_DCS,
    EVENT_APC,
    EVENT_PM,
    EVENT_SOS,
} EventType;


typedef struct {
    char priv;
    int arg[PARSER_CSI_ARGS];
    int narg;
    char final[3];
    int nfinal;
} CsiEvent;


typedef struct {
    int par;
    char *buf;
} StringEvent;


typedef struct {
    EventType type;
    union {
	char code;
	Rune r;
	CsiEvent csi;
	StringEvent str;
	struct {
	    uint8_t prefix;
	    uint8_t b;
	} esc;
    } data;
} Event;


typedef struct Parser Parser;


Parser *parser_new(void);
void parser_free(Parser * p);


int parser_feed(Parser * p, const char *buf, size_t len, Event * ev,
		int cap);
#endif
