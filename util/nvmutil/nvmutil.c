/* SPDX-License-Identifier: MIT
 *
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
#include <time.h>
#include <unistd.h>

#include "include/common.h"

int
main(int argc, char *argv[])
{
	struct xstate *x = xstatus(argc, argv);
	struct commands *cmd = &x->cmd[x->i];
	struct xfile *f = &x->f;

	unsigned long c;

	if (cmd->run == NULL)
		err(errno, "Command not set");

	cmd->run();

	for (c = 0; c < items(x->cmd); c++)
		x->cmd[c].run = cmd_helper_err;

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
