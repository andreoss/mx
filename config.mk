VERSION = 0.2.0

PREFIX = $(HOME)/.local

PKG_CONFIG ?= pkg-config

FEATURE != if [ "`uname -s`" = Linux ]; then echo -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600; else echo -D_BSD_SOURCE; fi
RTLIB != if [ "`uname -s`" = Linux ]; then echo -lrt; fi

INCS != $(PKG_CONFIG) --cflags cairo fontconfig xcb xproto xcb-keysyms xcb-xkb xkbcommon freetype2
LIBS != $(PKG_CONFIG) --libs cairo fontconfig xcb xcb-keysyms xcb-xkb xkbcommon freetype2

CFLAGS = -std=c99 -O2 -flto -Wall $(FEATURE) -DVERSION=\"$(VERSION)\" $(INCS) $(CPPFLAGS)
LDFLAGS = -flto $(LIBS) -lutil -lm $(RTLIB) $(LDFLAGS_EXTRA)
