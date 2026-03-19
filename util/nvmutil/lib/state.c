/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 *
 * This tool lets you modify Intel GbE NVM (Gigabit Ethernet
 * Non-Volatile Memory) images, e.g. change the MAC address.
 * These images configure your Intel Gigabit Ethernet adapter.
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
#include <unistd.h>

#include "../include/common.h"

/*
 * Initialise program state,
 * load GbE file and verify
 * data, ready for operation
 * (singleton design)
 */
struct xstate *
xstatus(int argc, char *argv[])
{
	static struct xstate us = {
	/* DO NOT MESS THIS UP, OR THERE WILL BE DEMONS */
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

	/* .cat (cat helpers set this) */
	-1

	};

	static int first_run = 1;

	if (!first_run)
		return &us;

	us.f.buf = us.f.real_buf;

	first_run = 0;
	us.argv0 = argv[0];

	if (argc > 1)
		us.f.fname = argv[1];

	if (argc < 3)
		usage();

/* https://man.openbsd.org/pledge.2
   https://man.openbsd.org/unveil.2     */
#if defined(__OpenBSD__) && defined(OpenBSD)
#if (OpenBSD) >= 604
	if (pledge("stdio flock rpath wpath cpath unveil", NULL) == -1)
		err(errno, "pledge plus unveil");
#elif (OpenBSD) >= 509
	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		err(errno, "pledge");
#endif
#endif

#ifndef S_ISREG
	err(ECANCELED, "Can't determine file types (S_ISREG undefined)");
#endif

#ifndef CHAR_BIT
	err(ECANCELED, "Unknown char size");
#else
	if (CHAR_BIT != 8)
		err(EINVAL, "Unsupported char size");
#endif

#if defined(__OpenBSD__) && defined(OpenBSD) && \
    (OpenBSD) >= 604
	/* can only use local tmp on openbsd, due to unveil */
	us.f.tname = new_tmpfile(&us.f.tmp_fd, 1, NULL);
#else
	us.f.tname = new_tmpfile(&us.f.tmp_fd, 0, NULL);
#endif
	if (us.f.tname == NULL)
		err(errno, "Can't create tmpfile");
	if (*us.f.tname == '\0')
		err(errno, "tmp dir is an empty string");

#if defined(__OpenBSD__) && defined(OpenBSD) && \
    OpenBSD >= 604
	if (unveil(f->tname, "rwc") == -1)
		err(errno, "unveil rwc: %s", f->tname);
#endif
	if (fstat(us.f.tmp_fd, &us.f.tmp_st) < 0)
		err(errno, "%s: stat", us.f.tname);

	sanitize_command_list();

	/* parse user command */
	set_cmd(argc, argv);
	set_cmd_args(argc, argv);

#if defined(__OpenBSD__) && defined(OpenBSD) && \
    (OpenBSD) >= 604
	if ((us.cmd[i].flags & O_ACCMODE) == O_RDONLY) {
		if (unveil(us.f.fname, "r") == -1)
			err(errno, "%s: unveil r", us.f.fname);
	} else {
		if (unveil(us.f.fname, "rwc") == -1)
			err(errno, "%s: unveil rw", us.f.tname);
	}

	if (unveil(us.f.tname, "rwc") == -1)
		err(errno, "%s: unveil rwc", us.f.tname);

	if (unveil(NULL, NULL) == -1)
		err(errno, "unveil block (rw)");

	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		err(errno, "pledge (kill unveil)");
#endif

	open_gbe_file();

	memset(us.f.real_buf, 0, sizeof(us.f.real_buf));
	memset(us.f.bufcmp, 0, sizeof(us.f.bufcmp));

	/* for good measure */
	memset(us.f.pad, 0, sizeof(us.f.pad));

	copy_gbe();
	read_checksums();

	return &us;
}

void
err(int nvm_errval, const char *msg, ...)
{
	struct xstate *x = xstatus(0, NULL);

	va_list args;

	if (errno == 0)
		errno = nvm_errval;
	if (!errno)
		errno = ECANCELED;

	(void)exit_cleanup();

	if (x != NULL)
		fprintf(stderr, "%s: ", getnvmprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	fprintf(stderr, ": %s\n", strerror(errno));

	exit(EXIT_FAILURE);
}

const char *
getnvmprogname(void)
{
	struct xstate *x = xstatus(0, NULL);

	const char *p;
	static char fallback[] = "nvmutil";

	char *rval = fallback;

	if (x != NULL) {
		if (x->argv0 == NULL || *x->argv0 == '\0')
			return "";

		rval = x->argv0;
	}

	p = strrchr(rval, '/');

	if (p)
		return p + 1;
	else
		return rval;
}

int
exit_cleanup(void)
{
	struct xstate *x = xstatus(0, NULL);
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
