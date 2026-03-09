# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2022,2026 Leah Rowe <leah@libreboot.org>
# SPDX-FileCopyrightText: 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CFLAGS?=-Os -Wall -Wextra -Werror -pedantic -std=c99 -D_POSIX_C_SOURCE=200809L
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install

# nvm is the old binary name,
# but it was renamed to nvmutil
# to avoid conflict with a certain
# package manager by that name!

nvmutil: nvmutil.c
	rm -f nvm
	$(CC) $(CFLAGS) nvmutil.c -o nvmutil

install:
	$(INSTALL) nvmutil $(DESTDIR)$(PREFIX)/bin/nvmutil

# do not delete *bin/nvm because
# there is a package manager by
# that name. this makefile now
# treats nvmutil as the binary

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/nvmutil

distclean:
	rm -f nvm nvmutil

clean:
	rm -f nvm nvmutil
