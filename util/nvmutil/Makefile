# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CSTD?=-std=c90
WERROR?=-Werror
CWARN?=-Wall -Wextra -pedantic
COPT?=-Os
CFLAGS?=$(CWARN) $(CSTD)
LDFLAGS?=
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install
LDIR?=-I.

OPTS=$(LDIR) $(COPT) $(WERROR) $(CFLAGS) $(LDFLAGS)

PROG=nvmutil

all: $(PROG)

$(PROG): nvmutil.c
	$(CC) $(OPTS) nvmutil.c -o $(PROG)

install: $(PROG)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG)

distclean: clean

.PHONY: all install uninstall clean distclean
