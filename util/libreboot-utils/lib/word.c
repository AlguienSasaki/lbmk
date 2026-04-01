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
nvm_word(size_t pos16, size_t p)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	size_t pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	return (unsigned short)f->buf[pos] |
	    ((unsigned short)f->buf[pos + 1] << 8);
}

void
set_nvm_word(size_t pos16, size_t p, unsigned short val16)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	size_t pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	f->buf[pos] = (unsigned char)(val16 & 0xff);
	f->buf[pos + 1] = (unsigned char)(val16 >> 8);

	set_part_modified(p);
}

void
set_part_modified(size_t p)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	check_bin(p, "part number");
	f->part_modified[p] = 1;
}

void
check_nvm_bound(size_t c, size_t p)
{
	/* Block out of bound NVM access
	 */

	check_bin(p, "part number");

	if (c >= NVM_WORDS)
		exitf("check_nvm_bound: out of bounds %lu",
		    (size_t)c);
}
