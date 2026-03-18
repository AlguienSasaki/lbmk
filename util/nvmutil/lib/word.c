/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 *
 * Manipulate sixteen-bit little-endian
 * words on Intel GbE NVM configurations.
 */

#ifdef __OpenBSD__
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../include/common.h"

/*
 * GbE NVM files store 16-bit (2-byte) little-endian words.
 * We must therefore swap the order when reading or writing.
 *
 * NOTE: The MAC address words are stored big-endian in the
 * file, but we assume otherwise and adapt accordingly.
 */

unsigned short
nvm_word(unsigned long pos16, unsigned long p)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	unsigned long pos;

	f = &x->f;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	return (unsigned short)f->buf[pos] |
	    ((unsigned short)f->buf[pos + 1] << 8);
}

void
set_nvm_word(unsigned long pos16, unsigned long p, unsigned short val16)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	unsigned long pos;

	f = &x->f;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	f->buf[pos] = (unsigned char)(val16 & 0xff);
	f->buf[pos + 1] = (unsigned char)(val16 >> 8);

	set_part_modified(p);
}

void
set_part_modified(unsigned long p)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	f = &x->f;

	check_bin(p, "part number");
	f->part_modified[p] = 1;
}

void
check_nvm_bound(unsigned long c, unsigned long p)
{
	/*
	 * NVM_SIZE assumed as the limit, because this
	 * current design assumes that we will only
	 * ever modified the NVM area.
	 */

	check_bin(p, "part number");

	if (c >= NVM_WORDS)
		err(ECANCELED, "check_nvm_bound: out of bounds %lu",
		    (unsigned long)c);
}
