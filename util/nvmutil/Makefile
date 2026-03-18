# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC = cc
HELLCC = clang

CFLAGS=
LDFLAGS=
DESTDIR=
PREFIX = /usr/local
INSTALL = install

.SUFFIXES: .c .o

LDIR=

PORTABLE = $(LDIR) $(CFLAGS)
WARN = $(PORTABLE) -Wall -Wextra
STRICT = $(WARN) -std=c90 -pedantic -Werror
HELLFLAGS = $(STRICT) -Weverything

PROG = nvmutil

# source files

SRCS = nvmutil.c state.c file.c string.c usage.c command.c num.c io.c \
    checksum.c word.c

# object files

OBJS = $(SRCS:.c=.o)

# default mode

MODE = portable

# default mode, options

CFLAGS_MODE = $(PORTABLE)
CC_MODE = $(CC)

# override modes (options)

ifeq ($(MODE),warn)
CFLAGS_MODE = $(WARN)
endif

ifeq ($(MODE),strict)
CFLAGS_MODE = $(STRICT)
endif

ifeq ($(MODE),hell)
CFLAGS_MODE = $(HELLFLAGS)
CC_MODE = $(HELLCC)
endif

# (rebuild on .h changes)
# (commented for portability)
#
# CFLAGS_MODE += -MMD -MP
# -include $(OBJS:.o=.d)
#
# likely more compatible,
# on its own:
# -include $(OBJS:.o=.d)
#
# i want this to build on
# old make, so i'll leave
# this blanked by default

all: $(PROG)

$(PROG): $(OBJS)
	$(CC_MODE) $(OBJS) -o $(PROG) $(LDFLAGS)

# generic rules

# .c.o: is the old style (portable)
# modern equivalent:
# %.o: %.c
#
.c.o:
	$(CC_MODE) $(CFLAGS_MODE) -c $< -o $@

# -m in install is not portable
# to old/other/weird versions.
# use chmod directly.
#
install: $(PROG)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG) $(OBJS) *.d

distclean: clean

# easy commands

warn:
	$(MAKE) MODE=warn

strict:
	$(MAKE) MODE=strict

hell:
	$(MAKE) MODE=hell

.PHONY: all warn strict hell install uninstall clean distclean
