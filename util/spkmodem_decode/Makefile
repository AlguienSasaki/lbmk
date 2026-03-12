# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2022,2026 Leah Rowe <leah@libreboot.org>
# SPDX-FileCopyrightText: 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CFLAGS?=-Os -Wall -Wextra -Werror -pedantic -std=c90
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install

PROG=spkmodem-decode

all: $(PROG)

$(PROG): spkmodem-decode.c
	$(CC) $(CFLAGS) spkmodem-decode.c -o $(PROG)

install: $(PROG)
	mkdir -p $(DESTDIR)$(PREFIX)/bin/
	install -c $(PROG) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG)

distclean: clean

.PHONY: all install uninstall clean distclean
