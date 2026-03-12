# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CFLAGS?=-Os -Wall -Wextra -Werror -pedantic -std=c90
LDFLAGS?=
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install

PROG=nvmutil

all: $(PROG)

$(PROG): nvmutil.c
	$(CC) $(CFLAGS) $(LDFLAGS) nvmutil.c -o $(PROG)

install: $(PROG)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 755 $(PROG) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG)

distclean: clean

.PHONY: all install uninstall clean distclean
