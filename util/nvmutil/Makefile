# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CSTD?=-std=c90
WERROR?=-Werror
CWARN?=-Wall -Wextra -pedantic
COPT?=-Os
CFLAGS?=
LDFLAGS?=
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install
LDIR?=-I.

OPTS?=$(LDIR) $(CFLAGS) $(LDFLAGS)

STRICTWARN?=$(OPTS) $(CWARN) $(CSTD) $(COPT)

STRICT?=$(STRICTWARN) $(WERROR)

# clang static analyser
HELLFLAGS?=$(STRICT) -Weverything

# program name
PROG=nvmutil

all: $(PROG)

$(PROG): $(PROG).c
	$(CC) $(OPTS) $(PROG).c -o $(PROG)

strictwarn: $(PROG).c
	$(CC) $(STRICTWARN) $(PROG).c -o $(PROG)

strict: $(PROG).c
	$(CC) $(STRICT) $(PROG).c -o $(PROG)

#clang static analyser
hell: $(PROG).c
	$(CC) $(HELLFLAGS) $(PROG).c -o $(PROG)

install: $(PROG)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG)

distclean: clean

.PHONY: all strictwarn strict hell install uninstall clean distclean
