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

int
main(int argc, char *argv[])
{
	struct xstate *x;
	struct commands *cmd;
	struct xfile *f;
	unsigned long i;

	char *tmp_path;

	struct stat st;
	int fd;

	tmp_path = NULL;

#ifndef S_ISREG
	err(ECANCELED, "Can't determine file types (S_ISREG undefined)");
#endif

#ifndef CHAR_BIT
	err(ECANCELED, "Unknown char size");
#else
	if (CHAR_BIT != 8)
		err(EINVAL, "Unsupported char size");
#endif

	if (argc < 3)
		usage();

#ifdef NVMUTIL_UNVEIL
	/*
	 * if global tmp is a different filesystem,
	 * unveil would trap on final file rename
	 * and we can't know the path in advance
	 */
	tmp_path = new_tmpfile(&fd, 1, NULL);
#else
	tmp_path = new_tmpfile(&fd, 0, NULL);
#endif

	if (tmp_path == NULL)
		err(errno, "Can't create tmpfile");

#ifdef NVMUTIL_PLEDGE
#ifdef NVMUTIL_UNVEIL
	if (pledge("stdio flock rpath wpath cpath unveil", NULL) == -1)
		err(errno, "pledge, unveil");
	if (unveil("/dev/null", "r") == -1)
		err(errno, "unveil: /dev/null");
#else
	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		err(errno, "pledge");
#endif
#endif

	x = xstatus();

	if (x == NULL)
		err(errno, NULL);
	if (x->f.buf == NULL)
		err(EINVAL, "Work buffer not initialised");

	x->argv0 = argv[0];
	f = &x->f;

	f->fname = argv[1];
	f->tname = tmp_path;
	f->tmp_fd = fd;

	if(fstat(fd, &st) < 0)
		err(errno, "can't stat tmpfile");

	f->tmp_dev = st.st_dev;
	f->tmp_ino = st.st_ino;

	sanitize_command_list();

	set_cmd(argc, argv);
	set_cmd_args(argc, argv);

	i = x->i;
	cmd = &x->cmd[i];

#ifdef NVMUTIL_UNVEIL
	if ((cmd->flags & O_ACCMODE) == O_RDONLY) {
		if (unveil(f->fname, "r") == -1)
			err(errno, "%s: unveil r", f->fname);
	} else {
		if (unveil(f->tname, "rwc") == -1)
			err(errno, "%s: unveil rw", f->tname);
	}

	if (unveil(f->tname, "rwc") == -1)
		err(errno, "%s: unveil rwc", f->tname);

	if (unveil(NULL, NULL) == -1)
		err(errno, "unveil block (rw)");

	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		err(errno, "pledge (kill unveil)");
#endif

	open_gbe_file();

	memset(f->buf, 0, GBE_BUF_SIZE);
	memset(f->bufcmp, 0, GBE_BUF_SIZE);

	copy_gbe();

	read_checksums();

	run_cmd();

	if ((cmd->flags & O_ACCMODE) == O_RDWR)
		write_to_gbe_bin();

	if (exit_cleanup() == -1)
		err(EIO, "%s: close", f->fname);

	if (f->io_err_gbe_bin)
		err(EIO, "%s: error writing final file");

	if (f->tname != NULL)
		free(f->tname);

	return EXIT_SUCCESS;
}
