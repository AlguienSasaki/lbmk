# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2022,2026 Leah Rowe <leah@libreboot.org>
# SPDX-FileCopyrightText: 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CFLAGS?=-Os -Wall -Wextra -Werror -pedantic -std=c99 -D_POSIX_C_SOURCE=200809L
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install

BIN=nvmutil

$(BIN): nvmutil.c
	$(CC) $(CFLAGS) nvmutil.c -o $(BIN)

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(BIN) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -f $(BIN)

distclean: clean

.PHONY: install uninstall clean distclean
