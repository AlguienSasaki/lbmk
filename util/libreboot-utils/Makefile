# SPDX-License-Identifier: MIT
# Copyright (c) 2022,2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>

CC = cc
CFLAGS = -Os -Wall -Wextra -std=c99 -pedantic
LDFLAGS =
PREFIX = /usr/local
DESTDIR =
INSTALL = install

PROGS = nvmutil mkhtemp lottery

OBJDIR = obj

# sources
LIB_SRCS = \
	lib/state.c \
	lib/file.c \
	lib/string.c \
	lib/usage.c \
	lib/command.c \
	lib/num.c \
	lib/io.c \
	lib/checksum.c \
	lib/word.c \
	lib/mkhtemp.c \
	lib/rand.c

NVMUTIL_SRCS = nvmutil.c $(LIB_SRCS)
MKHTEMP_SRCS = mkhtemp.c lib/file.c lib/string.c lib/num.c lib/mkhtemp.c lib/rand.c
LOTTERY_SRCS = lottery.c lib/file.c lib/string.c lib/num.c lib/mkhtemp.c lib/rand.c

# manually mapped objects (yes, explicit = portable)
OBJS_NVMUTIL = \
	obj/nvmutil.o \
	obj/lib/state.o \
	obj/lib/file.o \
	obj/lib/string.o \
	obj/lib/usage.o \
	obj/lib/command.o \
	obj/lib/num.o \
	obj/lib/io.o \
	obj/lib/checksum.o \
	obj/lib/word.o \
	obj/lib/mkhtemp.o \
	obj/lib/rand.o

OBJS_MKHTEMP = \
	obj/mkhtemp.o \
	obj/lib/file.o \
	obj/lib/string.o \
	obj/lib/num.o \
	obj/lib/mkhtemp.o \
	obj/lib/rand.o

OBJS_LOTTERY = \
	obj/lottery.o \
	obj/lib/file.o \
	obj/lib/string.o \
	obj/lib/num.o \
	obj/lib/mkhtemp.o \
	obj/lib/rand.o

all: $(PROGS)

nvmutil: $(OBJS_NVMUTIL)
	$(CC) $(CFLAGS) $(OBJS_NVMUTIL) -o $@ $(LDFLAGS)

mkhtemp: $(OBJS_MKHTEMP)
	$(CC) $(CFLAGS) $(OBJS_MKHTEMP) -o $@ $(LDFLAGS)

lottery: $(OBJS_LOTTERY)
	$(CC) $(CFLAGS) $(OBJS_LOTTERY) -o $@ $(LDFLAGS)

# ensure dirs exist
obj:
	mkdir -p obj/lib

# compile rules (EXPLICIT = portable)
obj/nvmutil.o: nvmutil.c | obj
	$(CC) $(CFLAGS) -c nvmutil.c -o $@

obj/mkhtemp.o: mkhtemp.c | obj
	$(CC) $(CFLAGS) -c mkhtemp.c -o $@

obj/lottery.o: lottery.c | obj
	$(CC) $(CFLAGS) -c lottery.c -o $@

obj/lib/state.o: lib/state.c | obj
	$(CC) $(CFLAGS) -c lib/state.c -o $@

obj/lib/file.o: lib/file.c | obj
	$(CC) $(CFLAGS) -c lib/file.c -o $@

obj/lib/string.o: lib/string.c | obj
	$(CC) $(CFLAGS) -c lib/string.c -o $@

obj/lib/usage.o: lib/usage.c | obj
	$(CC) $(CFLAGS) -c lib/usage.c -o $@

obj/lib/command.o: lib/command.c | obj
	$(CC) $(CFLAGS) -c lib/command.c -o $@

obj/lib/num.o: lib/num.c | obj
	$(CC) $(CFLAGS) -c lib/num.c -o $@

obj/lib/io.o: lib/io.c | obj
	$(CC) $(CFLAGS) -c lib/io.c -o $@

obj/lib/checksum.o: lib/checksum.c | obj
	$(CC) $(CFLAGS) -c lib/checksum.c -o $@

obj/lib/word.o: lib/word.c | obj
	$(CC) $(CFLAGS) -c lib/word.c -o $@

obj/lib/mkhtemp.o: lib/mkhtemp.c | obj
	$(CC) $(CFLAGS) -c lib/mkhtemp.c -o $@

obj/lib/rand.o: lib/rand.c | obj
	$(CC) $(CFLAGS) -c lib/rand.c -o $@

install: $(PROGS)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	for p in $(PROGS); do \
		$(INSTALL) $$p $(DESTDIR)$(PREFIX)/bin/$$p; \
		chmod 755 $(DESTDIR)$(PREFIX)/bin/$$p; \
	done

uninstall:
	for p in $(PROGS); do \
		rm -f $(DESTDIR)$(PREFIX)/bin/$$p; \
	done

clean:
	rm -rf $(OBJDIR) $(PROGS)

distclean: clean
