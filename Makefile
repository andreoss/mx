include config.mk

BINDIR = ./bin
TEST_DIR = t

OBJS = control.o parser.o \
            pty.o proc.o region.o screen.o term.o utf8.o \
            frontend-cairo.o frontend.o config.o main.o

TOBJS = utf8.o parser.o screen.o control.o term.o \
           region.o config.o

all: $(BINDIR)/mx libccor/libccor.a

libccor/libccor.a:
	cd libccor && $(MAKE)

$(BINDIR)/mx: $(OBJS) libccor/libccor.a
	mkdir -p $(BINDIR)
	$(CC) -o $@ $(OBJS) libccor/libccor.a $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_DIR)/mx-test: $(TOBJS) $(TEST_DIR)/mx-test.c libccor/libccor.a
	$(CC) $(CFLAGS) -I. -o $@ $(TEST_DIR)/mx-test.c $(TOBJS) libccor/libccor.a $(LDFLAGS) -lm

$(TEST_DIR)/test-region: $(TEST_DIR)/test-region.c region.o screen.o
	$(CC) -std=c99 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -I. -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -o $@ $(TEST_DIR)/test-region.c region.o screen.o $(LDFLAGS) -lm

test: test-mock test-region test-colour-correct

test-mock: all $(TEST_DIR)/mx-test
	@MOCK_TERM=./$(TEST_DIR)/mx-test perl $(TEST_DIR)/prove.pl

test-region: all $(TEST_DIR)/mx-test $(TEST_DIR)/test-region
	@./$(TEST_DIR)/test-region

test-colour-correct: libccor/libccor.a
	@cd libccor && $(MAKE) test

check: test

mx: $(BINDIR)/mx
	@:

clean:
	rm -rf $(BINDIR) *.o t/mx-test \
	  t/test-region
	cd libccor && $(MAKE) clean 2>/dev/null || true

dist: clean
	tar -czf term-$(VERSION).tar.gz *

indent:
	indent -kr *.c *.h

install: $(BINDIR)/mx
	install -D -m 755 $(BINDIR)/mx $(DESTDIR)$(PREFIX)/bin/mx

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mx
