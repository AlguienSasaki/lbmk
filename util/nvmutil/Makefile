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
	obj/state.o \
	obj/file.o \
	obj/string.o \
	obj/usage.o \
	obj/command.o \
	obj/num.o \
	obj/io.o \
	obj/checksum.o \
	obj/word.o

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

obj/nvmutil.o: nvmutil.c
	$(CC_MODE) $(CFLAGS_MODE) -c nvmutil.c -o obj/nvmutil.o

obj/state.o: state.c
	$(CC_MODE) $(CFLAGS_MODE) -c state.c -o obj/state.o

obj/file.o: file.c
	$(CC_MODE) $(CFLAGS_MODE) -c file.c -o obj/file.o

obj/string.o: string.c
	$(CC_MODE) $(CFLAGS_MODE) -c string.c -o obj/string.o

obj/usage.o: usage.c
	$(CC_MODE) $(CFLAGS_MODE) -c usage.c -o obj/usage.o

obj/command.o: command.c
	$(CC_MODE) $(CFLAGS_MODE) -c command.c -o obj/command.o

obj/num.o: num.c
	$(CC_MODE) $(CFLAGS_MODE) -c num.c -o obj/num.o

obj/io.o: io.c
	$(CC_MODE) $(CFLAGS_MODE) -c io.c -o obj/io.o

obj/checksum.o: checksum.c
	$(CC_MODE) $(CFLAGS_MODE) -c checksum.c -o obj/checksum.o

obj/word.o: word.c
	$(CC_MODE) $(CFLAGS_MODE) -c word.c -o obj/word.o

# install

install: $(PROG)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG) $(OBJS) *.d

distclean: clean

# mode targets (portable replacement for ifeq)

warn:
	$(MAKE) CFLAGS_MODE="$(WARN)"

strict:
	$(MAKE) CFLAGS_MODE="$(STRICT)"

hell:
	$(MAKE) CFLAGS_MODE="$(HELLFLAGS)" CC_MODE="$(HELLCC)"
