/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 *
 * Functions related to GbE NVM checksums.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

#include "../include/common.h"

void
read_checksums(void)
{
	struct xstate *x = xstatus();
	struct commands *cmd = &x->cmd[x->i];
	struct xfile *f = &x->f;

	size_t _p;
	size_t _skip_part;

	unsigned char _num_invalid;
	unsigned char _max_invalid;

	f->part_valid[0] = 0;
	f->part_valid[1] = 0;

	if (!cmd->chksum_read)
		return;

	_num_invalid = 0;
	_max_invalid = 2;

	if (cmd->arg_part)
		_max_invalid = 1;

	/* Skip verification on this part,
	 * but only when arg_part is set.
	 */
	_skip_part = f->part ^ 1;

	for (_p = 0; _p < 2; _p++) {

		/* Only verify a part if it was *read*
		 */
		if (cmd->arg_part && (_p == _skip_part))
			continue;

		f->part_valid[_p] = good_checksum(_p);
		if (!f->part_valid[_p])
			++_num_invalid;
	}

	if (_num_invalid >= _max_invalid) {

		if (_max_invalid == 1)
			exitf("%s: part %lu has a bad checksum",
			    f->fname, (size_t)f->part);

		exitf("%s: No valid checksum found in file",
		    f->fname);
	}
}

int
good_checksum(size_t partnum)
{
	unsigned short expected_checksum;
	unsigned short actual_checksum;

	expected_checksum =
	    calculated_checksum(partnum);

	actual_checksum =
	    nvm_word(NVM_CHECKSUM_WORD, partnum);

	if (expected_checksum == actual_checksum) {
		return 1;
	} else {
		return 0;
	}
}

void
set_checksum(size_t p)
{
	check_bin(p, "part number");
	set_nvm_word(NVM_CHECKSUM_WORD, p, calculated_checksum(p));
}

unsigned short
calculated_checksum(size_t p)
{
	size_t c;
	unsigned int val16;

	val16 = 0;

	for (c = 0; c < NVM_CHECKSUM_WORD; c++)
		val16 += (unsigned int)nvm_word(c, p);

	return (unsigned short)((NVM_CHECKSUM - val16) & 0xffff);
}
