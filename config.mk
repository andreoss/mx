VERSION = 0.1.0

PREFIX = $(HOME)/.local

PKGCFG := $(shell command -v pkg-config 2>/dev/null)
ifeq ($(PKGCFG),)
$(error "pkg-config not found. Run: nix develop --command make term")
endif
export PKG_CONFIG_PATH ?=
INCS = $(shell $(PKGCFG) --cflags cairo fontconfig xcb xproto xcb-keysyms xcb-xkb xkbcommon) -D_DEFAULT_SOURCE
LIBS = $(shell $(PKGCFG) --libs cairo fontconfig xcb xcb-keysyms xcb-xkb xkbcommon) -lutil -lm -lrt

INCS += $(shell $(PKGCFG) --cflags freetype2)
LIBS += $(shell $(PKGCFG) --libs freetype2)

CFLAGS = -std=c99 -O2 -Wall \
         -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 \
         -DVERSION=\"$(VERSION)\" \
         $(INCS) $(CPPFLAGS)
LDFLAGS = -Wl,--as-needed $(LIBS) $(LDFLAGS_EXTRA)
