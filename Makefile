CC ?= gcc
CFLAGS = -std=c11 -Wall -g -fPIC -pedantic -Wextra -Werror
LDFLAGS_STATIC = -g 
LDFLAGS = -arch arm64 -dynamiclib -fPIC

ifeq ($(CC), armv8m-tcc)
CFLAGS += -I. -I../../rootfs/usr/include -nostdlib -nostdinc 
LDFLAGS_STATIC += -Wl,-Ttext=0x0 -Wl,-section-alignment=0x4 -L../../rootfs/lib
endif
LDFLAGS += ${LDFLAGS_STATIC} 

SRCS = $(wildcard *.c)

OBJS = $(patsubst %.c, build/%.o, $(SRCS))

TARGET_SHARED = build/libncurses.so
TARGET_STATIC = build/libncurses.a

PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include

# Rules
all: $(TARGET_SHARED) $(TARGET_SHARED).elf $(TARGET_STATIC)

prepare: 
	mkdir -p build

build/%.o: %.c prepare
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(CC), armv8m-tcc)
$(TARGET_SHARED): $(OBJS)
	$(CC) $(LDFLAGS) -Wl,-oformat=yaff $^ -o $@
else 
$(TARGET_SHARED): 

endif 

$(TARGET_SHARED).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(TARGET_STATIC): $(OBJS)
	ar rcs $@ $^

install: $(TARGET_SHARED) $(TARGET_STATIC) $(TARGET_SHARED).elf
	mkdir -p $(LIBDIR)
	cp $(TARGET_SHARED) $(LIBDIR)
	cp $(TARGET_STATIC) $(LIBDIR)

	cp include/*.h $(INCLUDEDIR)

clean:
	rm -rf build