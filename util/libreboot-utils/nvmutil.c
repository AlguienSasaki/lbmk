/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 *
 * This tool lets you modify Intel GbE NVM (Gigabit Ethernet
 * Non-Volatile Memory) images, e.g. change the MAC address.
 * These images configure your Intel Gigabit Ethernet adapter.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/common.h"

int
main(int argc, char *argv[])
{
	struct xstate *x;

	struct commands *cmd;
	struct xfile *f;

	size_t c;

	if (lbgetprogname(argv[0]) == NULL)
		err_no_cleanup(0, errno, "could not set progname");

	xpledgex("stdio flock rpath wpath cpath unveil", NULL);
	xunveilx("/dev/urandom", "r");

#ifndef S_ISREG
	err_no_cleanup(0, ECANCELED,
	    "Can't determine file types (S_ISREG undefined)");
#endif
#if ((CHAR_BIT) != 8)
	err_no_cleanup(0, ECANCELED, "Unsupported char size");
#endif

	if ((x = xstart(argc, argv)) == NULL)
		err_no_cleanup(0, ECANCELED, "NULL state on init");

	/* parse user command */
/* TODO: CHECK ACCESSES VIA xstatus() */
	set_cmd(argc, argv);
	set_cmd_args(argc, argv);

	cmd = &x->cmd[x->i];
	f = &x->f;

	if ((cmd->flags & O_ACCMODE) == O_RDONLY)
		xunveilx(f->fname, "r");
	else
		xunveilx(f->fname, "rwc");

	xunveilx(f->tname, "rwc");
	xunveilx(NULL, NULL);
	xpledgex("stdio flock rpath wpath cpath", NULL);

	if (cmd->run == NULL)
		b0rk(errno, "Command not set");

	sanitize_command_list();

	open_gbe_file();

	copy_gbe();
	read_checksums();

	cmd->run();

	for (c = 0; c < items(x->cmd); c++)
		x->cmd[c].run = cmd_helper_err;

	if ((cmd->flags & O_ACCMODE) == O_RDWR)
		write_to_gbe_bin();

	if (exit_cleanup() == -1)
		b0rk(EIO, "%s: close", f->fname);

	if (f->io_err_gbe_bin)
		b0rk(EIO, "%s: error writing final file");

	free_and_set_null(&f->tname);

	return EXIT_SUCCESS;
}
