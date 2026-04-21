# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2022,2025 Leah Rowe <leah@libreboot.org>
# SPDX-FileCopyrightText: 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CFLAGS?=-Os -Wall -Wextra -Werror -pedantic
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install

nvm: nvmutil.c
	$(CC) $(CFLAGS) nvmutil.c -o nvm

install:
	$(INSTALL) nvm $(DESTDIR)$(PREFIX)/bin/nvm

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/nvm

distclean:
	rm -f nvm

clean:
	rm -f nvm
