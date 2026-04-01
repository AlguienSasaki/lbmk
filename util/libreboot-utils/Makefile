# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

# Makefile for nvmutil, which is an application
# that modifies Intel GbE NVM configurations.

CC = cc
HELLCC = clang

CFLAGS = -Os -Wall -Wextra -std=c99 -pedantic
LDFLAGS =
DESTDIR =
PREFIX = /usr/local
INSTALL = install

# used for portability testing on linux:
#
PORT_OPENAT = -DUSE_OPENAT=1
PORT_ARC4 = -DUSE_ARC4=1
PORT_URANDOM = -DUSE_URANDOM=1

.SUFFIXES: .c .o

LDIR =

HELLFLAGS = $(STRICT) -Weverything

PROG = nvmutil
PROGMKH = mkhtemp
PROGLOT = lottery

OBJS_NVMUTIL = \
	obj/nvmutil.o \
	obj/lib/state.o \
	obj/lib/file.o \
	obj/lib/string.o \
	obj/lib/usage.o \
	obj/lib/command.o \
	obj/lib/num.o \
	obj/lib/io.o \
	obj/lib/checksum.o \
	obj/lib/word.o \
	obj/lib/mkhtemp.o \
	obj/lib/rand.o

OBJS_MKHTEMP = \
	obj/mkhtemp.o \
	obj/lib/file.o \
	obj/lib/string.o \
	obj/lib/num.o \
	obj/lib/mkhtemp.o \
	obj/lib/rand.o

OBJS_LOTTERY = \
	obj/lottery.o \
	obj/lib/file.o \
	obj/lib/string.o \
	obj/lib/num.o \
	obj/lib/mkhtemp.o \
	obj/lib/rand.o

# default mode
CC_MODE = $(CC)

all: $(PROG) $(PROGMKH) $(PROGLOT)

$(PROG): $(OBJS_NVMUTIL)
	$(CC_MODE) $(CFLAGS) $(OBJS_NVMUTIL) -o $(PROG) $(LDFLAGS)

$(PROGMKH): $(OBJS_MKHTEMP)
	$(CC_MODE) $(CFLAGS) $(OBJS_MKHTEMP) -o $(PROGMKH) $(LDFLAGS)

$(PROGLOT): $(OBJS_LOTTERY)
	$(CC_MODE) $(CFLAGS) $(OBJS_LOTTERY) -o $(PROGLOT) $(LDFLAGS)

# ensure obj directory exists
$(OBJS_NVMUTIL): obj
$(OBJS_MKHTEMP): obj
$(OBJS_LOTTERY): obj

obj:
	mkdir obj || true
	mkdir obj/lib || true

# main program object

obj/nvmutil.o: nvmutil.c
	$(CC_MODE) $(CFLAGS) -c nvmutil.c -o obj/nvmutil.o

obj/mkhtemp.o: mkhtemp.c
	$(CC_MODE) $(CFLAGS) -c mkhtemp.c -o obj/mkhtemp.o

obj/lottery.o: lottery.c
	$(CC_MODE) $(CFLAGS) -c lottery.c -o obj/lottery.o

# library/helper objects

obj/lib/state.o: lib/state.c
	$(CC_MODE) $(CFLAGS) -c lib/state.c -o obj/lib/state.o

obj/lib/file.o: lib/file.c
	$(CC_MODE) $(CFLAGS) -c lib/file.c -o obj/lib/file.o

obj/lib/string.o: lib/string.c
	$(CC_MODE) $(CFLAGS) -c lib/string.c -o obj/lib/string.o

obj/lib/usage.o: lib/usage.c
	$(CC_MODE) $(CFLAGS) -c lib/usage.c -o obj/lib/usage.o

obj/lib/command.o: lib/command.c
	$(CC_MODE) $(CFLAGS) -c lib/command.c -o obj/lib/command.o

obj/lib/num.o: lib/num.c
	$(CC_MODE) $(CFLAGS) -c lib/num.c -o obj/lib/num.o

obj/lib/io.o: lib/io.c
	$(CC_MODE) $(CFLAGS) -c lib/io.c -o obj/lib/io.o

obj/lib/checksum.o: lib/checksum.c
	$(CC_MODE) $(CFLAGS) -c lib/checksum.c -o obj/lib/checksum.o

obj/lib/word.o: lib/word.c
	$(CC_MODE) $(CFLAGS) -c lib/word.c -o obj/lib/word.o

obj/lib/mkhtemp.o: lib/mkhtemp.c
	$(CC_MODE) $(CFLAGS) -c lib/mkhtemp.c -o obj/lib/mkhtemp.o

obj/lib/rand.o: lib/rand.c
	$(CC_MODE) $(CFLAGS) -c lib/rand.c -o obj/lib/rand.o

# install

install: $(PROG) $(PROGMKH) $(PROGLOT)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)
	$(INSTALL) $(PROGMKH) $(DESTDIR)$(PREFIX)/bin/$(PROGMKH)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROGMKH)
	$(INSTALL) $(PROGLOT) $(DESTDIR)$(PREFIX)/bin/$(PROGLOT)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROGLOT)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGMKH)
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGLOT)

clean:
	rm -f $(PROG) $(PROGMKH) $(OBJS_NVMUTIL) $(OBJS_MKHTEMP) \
	    $(OBJS_LOTTERY) $(PROGLOT)

distclean: clean

# mode targets (portable replacement for ifeq)

strict:
	$(MAKE) CFLAGS="$(CFLAGS) $(HELLFLAGS)" CC_MODE="$(HELLCC)"

# BSD-like portability test (openat + arc4random)
portable-bsd:
	$(MAKE) CFLAGS="$(CFLAGS) $(PORT_OPENAT) $(PORT_ARC4)" CC_MODE="$(CC)"

# fallback portability test (openat + urandom -- old linux mostly)
portable-urandom:
	$(MAKE) CFLAGS="$(CFLAGS) $(PORT_OPENAT) $(PORT_URANDOM)" \
	    CC_MODE="$(CC)"
