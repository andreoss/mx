VERSION = 0.1.0

PREFIX = $(HOME)/.local

PKG_CONFIG ?= pkg-config

FEATURE != if [ "`uname -s`" = Linux ]; then echo -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600; else echo -D_BSD_SOURCE; fi
RTLIB != if [ "`uname -s`" = Linux ]; then echo -lrt; fi

USE_PANGO ?= 1
PANGO_PKGS_1 = harfbuzz
PANGO_PKGS = $(PANGO_PKGS_$(USE_PANGO))
PANGO_DEF_1 = -DUSE_PANGO
PANGO_DEF = $(PANGO_DEF_$(USE_PANGO))

INCS != $(PKG_CONFIG) --cflags cairo fontconfig xcb xproto xcb-keysyms xcb-xkb xkbcommon freetype2 $(PANGO_PKGS)
LIBS != $(PKG_CONFIG) --libs cairo fontconfig xcb xcb-keysyms xcb-xkb xkbcommon freetype2 $(PANGO_PKGS)

CFLAGS = -std=c99 -O2 -Wall $(FEATURE) $(PANGO_DEF) -DVERSION=\"$(VERSION)\" $(INCS) $(CPPFLAGS)
LDFLAGS = $(LIBS) -lutil -lm $(RTLIB) $(LDFLAGS_EXTRA)
