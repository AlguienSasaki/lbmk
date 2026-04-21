# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

# Makefile for nvmutil, which is an application
# that modifies Intel GbE NVM configurations.

CC = cc
HELLCC = clang

CFLAGS =
LDFLAGS =
DESTDIR =
PREFIX = /usr/local
INSTALL = install

.SUFFIXES: .c .o

LDIR =

PORTABLE = $(LDIR) $(CFLAGS)
WARN = $(PORTABLE) -Wall -Wextra
STRICT = $(WARN) -std=c90 -pedantic -Werror
HELLFLAGS = $(STRICT) -Weverything

PROG = nvmutil

OBJS = \
	obj/nvmutil.o \
	obj/lib/state.o \
	obj/lib/file.o \
	obj/lib/string.o \
	obj/lib/usage.o \
	obj/lib/command.o \
	obj/lib/num.o \
	obj/lib/io.o \
	obj/lib/checksum.o \
	obj/lib/word.o

# default mode
CFLAGS_MODE = $(PORTABLE)
CC_MODE = $(CC)

all: $(PROG)

$(PROG): $(OBJS)
	$(CC_MODE) $(OBJS) -o $(PROG) $(LDFLAGS)

# ensure obj directory exists
$(OBJS): obj

obj:
	mkdir obj || true
	mkdir obj/lib || true

# main program object

obj/nvmutil.o: nvmutil.c
	$(CC_MODE) $(CFLAGS_MODE) -c nvmutil.c -o obj/nvmutil.o

# library/helper objects

obj/lib/state.o: lib/state.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/state.c -o obj/lib/state.o

obj/lib/file.o: lib/file.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/file.c -o obj/lib/file.o

obj/lib/string.o: lib/string.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/string.c -o obj/lib/string.o

obj/lib/usage.o: lib/usage.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/usage.c -o obj/lib/usage.o

obj/lib/command.o: lib/command.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/command.c -o obj/lib/command.o

obj/lib/num.o: lib/num.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/num.c -o obj/lib/num.o

obj/lib/io.o: lib/io.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/io.c -o obj/lib/io.o

obj/lib/checksum.o: lib/checksum.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/checksum.c -o obj/lib/checksum.o

obj/lib/word.o: lib/word.c
	$(CC_MODE) $(CFLAGS_MODE) -c lib/word.c -o obj/lib/word.o

# install

install: $(PROG)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG) $(OBJS)

distclean: clean

# mode targets (portable replacement for ifeq)

warn:
	$(MAKE) CFLAGS_MODE="$(WARN)"

strict:
	$(MAKE) CFLAGS_MODE="$(STRICT)"

hell:
	$(MAKE) CFLAGS_MODE="$(HELLFLAGS)" CC_MODE="$(HELLCC)"
