# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC = cc
CFLAGS = -Os -Wall -Wextra -std=c99 -pedantic
LDFLAGS =
PREFIX = /usr/local
DESTDIR =
INSTALL = install

PROGS = nvmutil

LIB_OBJS = \
	lib/state.o \
	lib/file.o \
	lib/string.o \
	lib/usage.o \
	lib/command.o \
	lib/num.o \
	lib/io.o \
	lib/checksum.o \
	lib/word.o

OBJS_NVMUTIL = nvmutil.o $(LIB_OBJS)

all: $(PROGS)

nvmutil: $(OBJS_NVMUTIL)
	$(CC) $(CFLAGS) $(OBJS_NVMUTIL) -o $@ $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

install: $(PROGS)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	for p in $(PROGS); do \
		$(INSTALL) $$p $(DESTDIR)$(PREFIX)/bin/$$p; \
		chmod 755 $(DESTDIR)$(PREFIX)/bin/$$p; \
	done

uninstall:
	for p in $(PROGS); do \
		rm -f $(DESTDIR)$(PREFIX)/bin/$$p; \
	done

clean:
	rm -f $(PROGS) *.o lib/*.o

distclean: clean
