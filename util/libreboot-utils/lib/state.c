/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 *
 * State machine (singleton) for nvmutil data.
 */

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
#include <unistd.h>

#include "../include/common.h"

struct xstate *
xstart(int argc, char *argv[])
{
	static int first_run = 1;
	static char *dir = NULL;
	static char *base = NULL;
	char *realdir = NULL;
	char *tmpdir = NULL;
	char *tmpbase_local = NULL;

	static struct xstate us = {
	{
		/* be careful when modifying xstate. you
		 * must set everything precisely */
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

	/* ->i   (index to cmd[]) */
	0,

	/* .no_cmd (set 0 when a command is found) */
	1,

	/* .cat (cat helpers set this) */
	-1

	};

	if (!first_run)
		return &us;

	if (argc < 3)
		err_exit(EINVAL, "xstart: Too few arguments");
	if (argv == NULL)
		err_exit(EINVAL, "xstart: NULL argv");
	
	first_run = 0;

	us.f.buf = us.f.real_buf;

	us.f.fname = argv[1];

	us.f.tmp_fd = -1;
	us.f.tname = NULL;

	if ((realdir = realpath(us.f.fname, NULL)) == NULL)
		err_exit(errno, "xstart: can't get realpath of %s",
		    us.f.fname);

	if (fs_dirname_basename(realdir, &dir, &base, 0) < 0)
		err_exit(errno, "xstart: don't know CWD of %s",
		    us.f.fname);

	if ((us.f.base = strdup(base)) == NULL)
		err_exit(errno, "strdup base");

	us.f.dirfd = fs_open(dir,
	    O_RDONLY | O_DIRECTORY);
	if (us.f.dirfd < 0)
		err_exit(errno, "%s: open dir", dir);

	if (new_tmpfile(&us.f.tmp_fd, &us.f.tname, dir, ".gbe.XXXXXXXXXX") < 0)
		err_exit(errno, "%s", us.f.tname);

	if (fs_dirname_basename(us.f.tname,
	    &tmpdir, &tmpbase_local, 0) < 0)
		err_exit(errno, "tmp basename");

	us.f.tmpbase = strdup(tmpbase_local);
	if (us.f.tmpbase == NULL)
		err_exit(errno, "strdup tmpbase");

	free_and_set_null(&tmpdir);

	if (us.f.tname == NULL)
		err_exit(errno, "x->f.tname null");
	if (*us.f.tname == '\0')
		err_exit(errno, "x->f.tname empty");

	if (fstat(us.f.tmp_fd, &us.f.tmp_st) < 0)
		err_exit(errno, "%s: stat", us.f.tname);

	memset(us.f.real_buf, 0, sizeof(us.f.real_buf));
	memset(us.f.bufcmp, 0, sizeof(us.f.bufcmp));

	/* for good measure */
	memset(us.f.pad, 0, sizeof(us.f.pad));

	return &us;
}

struct xstate *
xstatus(void)
{
	struct xstate *x = xstart(0, NULL);

	if (x == NULL)
		err_exit(EACCES, "NULL pointer to xstate");

	return x;
}
