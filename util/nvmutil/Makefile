# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2022,2025 Leah Rowe <leah@libreboot.org>
# SPDX-FileCopyrightText: 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC?=cc
CFLAGS?=-Os -Wall -Wextra
DESTDIR?=
PREFIX?=/usr/local
INSTALL?=install

nvmutil: nvmutil.c
	$(CC) $(CFLAGS) nvmutil.c -o nvmutil

install:
	$(INSTALL) nvmutil $(DESTDIR)$(PREFIX)/bin/nvmutil

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/nvmutil

distclean:
	rm -f nvmutil

clean:
	rm -f nvmutil
