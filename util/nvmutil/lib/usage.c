/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
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

#include "../include/common.h"

void
usage(void)
{
	const char *util;

	util = getnvmprogname();

	fprintf(stderr,
	    "Modify Intel GbE NVM images e.g. set MAC\n"
	    "USAGE:\n"
	    "\t%s FILE dump\n"
	    "\t%s FILE setmac [MAC]\n"
	    "\t%s FILE swap\n"
	    "\t%s FILE copy 0|1\n"
	    "\t%s FILE cat\n"
	    "\t%s FILE cat16\n"
	    "\t%s FILE cat128\n",
	    util, util, util, util,
	    util, util, util);

	err(EINVAL, "Too few arguments");
}
