# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CFLAGS?=
LDFLAGS?=
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install

.SUFFIXES:

# maybe add -I. here when running make
# e.g. make LDIR=-I.
LDIR?=

PORTABLE?=$(LDIR) $(CFLAGS)
WARN?=$(PORTABLE) -Wall -Wextra
STRICT?=$(WARN) -std=c90 -pedantic -Werror
HELLFLAGS?=$(STRICT) -Weverything

# program name
PROG=nvmutil

all: $(PROG)

$(PROG): $(PROG).c
	$(CC) $(PORTABLE) $(PROG).c -o $(PROG) $(LDFLAGS)

warn: $(PROG).c
	$(CC) $(WARN) $(PROG).c -o $(PROG) $(LDFLAGS)

strict: $(PROG).c
	$(CC) $(STRICT) $(PROG).c -o $(PROG) $(LDFLAGS)

# clang-only extreme warnings (not portable)
hell: $(PROG).c
	$(CC) $(HELLFLAGS) $(PROG).c -o $(PROG) $(LDFLAGS)

install: $(PROG)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG)

distclean: clean

.PHONY: all warn strict hell install uninstall clean distclean
