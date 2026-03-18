/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 *
 * This tool lets you modify Intel GbE NVM (Gigabit Ethernet
 * Non-Volatile Memory) images, e.g. change the MAC address.
 * These images configure your Intel Gigabit Ethernet adapter.
 *
 * This code is designed to be portable, running on as many
 * Unix and Unix-like systems as possible (mainly BSD/Linux).
 *
 * Recommended CFLAGS for Clang/GCC:
 *
 * -Os -Wall -Wextra -Werror -pedantic -std=c90
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

#include "include/common.h"

/*
 * Program state/command table
 * Default config stored here,
 * and copied to a newly allocated
 * buffer in memory, then the pointer
 * is passed. The rest of the program
 * will manipulate this data.
 */
/*
TODO: 
eventually, i will not have this return
a pointer at all. instead, a similar key
mechanism will be used for other access
functions e.g. word/set_word, err(needs
to clean up), and so on. then those
would return values if already initialised,
but would not permit additional init - will
decide exactly how to implement this at a
later state.

this is part of an ongoing effort to introduce
extreme memory safety into this program.
 */
struct xstate *
xstatus(void)
{
	static int first_run = 1;

	static struct xstate us = {
	/* .cmd (update cmd[] in the struct if adding to it)
	         DO NOT FORGET. or C will init zeroes/NULLs */

	/* cmd[] members */
		/* DO NOT MESS THIS UP */
		/* items must be set *exactly* */
	{
	{
		CMD_DUMP, "dump", cmd_helper_dump, ARGC_3,
		ARG_NOPART,
		SKIP_CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		NVM_SIZE, O_RDONLY
	}, {
		CMD_SETMAC, "setmac", cmd_helper_setmac, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, CHECKSUM_WRITE,
		NVM_SIZE, O_RDWR
	}, {
		CMD_SWAP, "swap", cmd_helper_swap, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDWR
	}, {
		CMD_COPY, "copy", cmd_helper_copy, ARGC_4,
		ARG_PART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDWR
	}, {
		CMD_CAT, "cat", cmd_helper_cat, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDONLY
	}, {
		CMD_CAT16, "cat16", cmd_helper_cat16, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDONLY
	}, {
		CMD_CAT128, "cat128", cmd_helper_cat128, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDONLY
	}
	},

	/* ->mac */
	{NULL, "xx:xx:xx:xx:xx:xx", {0, 0, 0}}, /* .str, .rmac, .mac_buf */

	/* .f */
	{0},

	/* .argv0 (for our getprogname implementation) */
	NULL,

	/* ->i   (index to cmd[]) */
	0,

	/* .no_cmd (set 0 when a command is found) */
	1,

	/* .xsize (size of the stuct will be stored here later) */
	0,

	/* .cat (cat helpers set this) */
	-1

	};

	if (!first_run)
		return &us;

	first_run = 0;

	us.xsize = sizeof(us);

	us.f.buf = us.f.real_buf;

	us.f.gbe_fd = -1;
	us.f.tmp_fd = -1;

	us.f.tname = NULL;
	us.f.fname = NULL;

	return &us;
}

int
exit_cleanup(void)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	int close_err;
	int saved_errno;

	close_err = 0;
	saved_errno = errno;

	if (x != NULL) {
		f = &x->f;

		if (f->gbe_fd > -1) {
			if (x_i_close(f->gbe_fd) == -1)
				close_err = 1;
			f->gbe_fd = -1;
		}

		if (f->tmp_fd > -1) {
			if (x_i_close(f->tmp_fd) == -1)
				close_err = 1;
		}

		if (f->tname != NULL) {
			if (unlink(f->tname) == -1)
				close_err = 1;
		}

		f->tmp_fd = -1;
	}

	if (saved_errno)
		errno = saved_errno;

	if (close_err)
		return -1;

	return 0;
}
