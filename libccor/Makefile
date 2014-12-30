CC ?= gcc
CFLAGS = -std=c99 -O2 -Wall -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
AR = ar
ARFLAGS = rcs

OBJS = ccor.o
TARGET = libccor.a
TEST_BIN = ccor-test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

ccor.o: ccor.c ccor.h

$(TEST_BIN): $(TEST_BIN).c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $(TEST_BIN).c $(TARGET) -lm

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_BIN)

.PHONY: all test clean
