CC ?= gcc
CFLAGS = -std=c11 -Wall -g -fPIC -pedantic -Wextra 
LDFLAGS_STATIC = -g 
LDFLAGS = -shared -fPIC

ifeq ($(CC), armv8m-tcc)
CFLAGS += -I. -I../../rootfs/usr/include -nostdlib -nostdinc 
LDFLAGS_STATIC += -Wl,-Ttext=0x0 -Wl,-section-alignment=0x4 -L../../rootfs/lib
TARGET_SHARED = build/libncurses.so
TARGET_SHARED_ELF = build/libncurses.so.elf
else 
TARGET_SHARED = build/libncurses_yaff.so
TARGET_SHARED_ELF = build/libncurses.so
endif


LDFLAGS += ${LDFLAGS_STATIC} 

TARGET_STATIC = build/libncurses.a
SRCS = $(wildcard *.c)

OBJS = $(patsubst %.c, build/%.o, $(SRCS))

PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include

# Rules
all: $(TARGET_SHARED) $(TARGET_SHARED_ELF) $(TARGET_STATIC)

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

$(TARGET_SHARED_ELF): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(TARGET_STATIC): $(OBJS)
	ar rcs $@ $^

install: $(TARGET_SHARED) $(TARGET_STATIC) $(TARGET_SHARED_ELF)
	mkdir -p $(LIBDIR)
	cp $(TARGET_SHARED) $(LIBDIR)
	cp $(TARGET_STATIC) $(LIBDIR)

	cp include/*.h $(INCLUDEDIR)

clean:
	rm -rf build