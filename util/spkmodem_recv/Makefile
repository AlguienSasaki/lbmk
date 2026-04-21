# SPDX-License-Identifier: GPL-2.0-or-later
CC?=cc
CFLAGS?=-Os -Wall -Wextra -Werror -pedantic
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install

spkmodem-recv:
	$(CC) $(CFLAGS) -o $@ $@.c
install: spkmodem-recv
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) $< -t $(DESTDIR)$(PREFIX)/bin/
clean:
	rm -f spkmodem-recv
