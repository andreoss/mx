CC ?= gcc
CFLAGS = -std=c99 -O2 -Wall -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
AR = ar
ARFLAGS = rcs

SRCS = ccor.c
OBJS = $(SRCS:.c=.o)
TARGET = libccor.a
TEST_BIN = ccor-test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

%.o: %.c ccor.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_BIN).c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $(TEST_BIN).c $(TARGET) -lm

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_BIN)

.PHONY: all test clean