/* SPDX-License-Identifier: MIT                                        ( >:3 )
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>               /| |\
 *                                                                       / \
 * This tool lets you modify Intel GbE NVM (Gigabit Ethernet
 * Non-Volatile Memory) images, e.g. change the MAC address.
 * These images configure your Intel Gigabit Ethernet adapter.
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

#include "include/common.h"

static void
exit_cleanup(void);

int
main(int argc, char *argv[])
{
	struct xstate *x;
	struct commands *cmd;
	struct xfile *f;
	size_t c;

	(void) lbsetprogname(argv[0]);
	if (argc < 3)
		usage();

	(void) errhook(exit_cleanup);

	/* https://man.openbsd.org/pledge.2 */
	/* https://man.openbsd.org/unveil.2 */
	xpledgex("stdio flock rpath wpath cpath unveil", NULL);
	xunveilx("/dev/urandom", "r");

#ifndef S_ISREG
	exitf(
	    "Can't determine file types (S_ISREG undefined)");
#endif
#if ((CHAR_BIT) != 8)
	exitf("Unsupported char size");
#endif

	if ((x = xstart(argc, argv)) == NULL)
		exitf("NULL state on init");

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
		exitf("Command not set");

	sanitize_command_list();
	open_gbe_file();
	copy_gbe();
	read_checksums();
	cmd->run();

	for (c = 0; c < items(x->cmd); c++)
		x->cmd[c].run = cmd_helper_err;

	if ((cmd->flags & O_ACCMODE) == O_RDWR)
		write_to_gbe_bin();

	exit_cleanup();
	if (f->io_err_gbe_bin)
		exitf("%s: error writing final file");

	free_and_set_null(&f->tname);

	return EXIT_SUCCESS;
}

static void
exit_cleanup(void)
{
	struct xstate *x;
	struct xfile *f;

	x = xstatus();
	if (x == NULL)
		return;

	f = &x->f;

	/* close fds if still open */
	xclose(&f->tmp_fd);
	xclose(&f->gbe_fd);

	/* unlink tmpfile if it exists */
	if (f->tname != NULL) {
		(void) unlink(f->tname);
		free_and_set_null(&f->tname);
	}
}
