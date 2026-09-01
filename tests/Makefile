CC = gcc
CFLAGS = -Wall -Wextra -pedantic -g -std=gnu11 -I../include

SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, build/%.o, $(SRCS))

TARGET = build/curses_tests
CURSES_SRCS = $(wildcard ../*.c) $(wildcard ../include/*.h)

# Rules
all: $(TARGET) test

build/%.o: %.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# Build the library under test for the host, out of the device build tree.
build/curses.o: ../curses.c $(CURSES_SRCS)
	mkdir -p build
	$(CC) $(CFLAGS) -c ../curses.c -o $@

$(TARGET): $(OBJS) build/curses.o
	$(CC) $(CFLAGS) $^ -o $@

test: $(TARGET)
	./$(TARGET)

build_tests: $(TARGET)

clean:
	rm -rf build
