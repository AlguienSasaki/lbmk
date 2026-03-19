/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 *
 * Manipulate Intel GbE NVM words, which are 16-bit little
 * endian in the files (MAC address words are big endian).
 */

#include <sys/types.h>

#include <errno.h>
#include <stddef.h>

#include "../include/common.h"

unsigned short
nvm_word(unsigned long pos16, unsigned long p)
{
	struct xstate *x = xstatus(0, NULL);
	struct xfile *f = &x->f;

	unsigned long pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	return (unsigned short)f->buf[pos] |
	    ((unsigned short)f->buf[pos + 1] << 8);
}

void
set_nvm_word(unsigned long pos16, unsigned long p, unsigned short val16)
{
	struct xstate *x = xstatus(0, NULL);
	struct xfile *f = &x->f;

	unsigned long pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	f->buf[pos] = (unsigned char)(val16 & 0xff);
	f->buf[pos + 1] = (unsigned char)(val16 >> 8);

	set_part_modified(p);
}

void
set_part_modified(unsigned long p)
{
	struct xstate *x = xstatus(0, NULL);
	struct xfile *f = &x->f;

	check_bin(p, "part number");
	f->part_modified[p] = 1;
}

void
check_nvm_bound(unsigned long c, unsigned long p)
{
	/* Block out of bound NVM access
	 */

	check_bin(p, "part number");

	if (c >= NVM_WORDS)
		err(ECANCELED, "check_nvm_bound: out of bounds %lu",
		    (unsigned long)c);
}
