/* SPDX-License-Identifier: MIT                                        ( >:3 )
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>                    /| |\
 *                                                                       / \
 * Hardened mktemp (mkhtemp!)
 *
 * WORK IN PROGRESS (proof of concept), or, v0.0000001
 * DO NOT PUT THIS IN YOUR LINUX DISTRO YET.
 *
 * In other words: for reference only -- PATCHES WELCOME!
 *
 * I will remove this notice when the code is mature, and
 * probably contact several of your projects myself.
 *
 * See README. This is an ongoing project; no proper docs
 * yet, and no manpage (yet!) - the code is documentation,
 * while the specification that it implements evolves.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
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

#include "include/common.h"

static void
exit_cleanup(void);

int
main(int argc, char *argv[])
{
	size_t len;
	size_t tlen;
	size_t xc = 0;

	char *tmpdir = NULL;
	char *template = NULL;
	char *p;
	char *s = NULL;
	char *rp;
	char resolved[PATH_MAX];
	char c;

	int fd = -1;
	int type = MKHTEMP_FILE;

	(void) errhook(exit_cleanup);
	(void) lbsetprogname(argv[0]);

	/* https://man.openbsd.org/pledge.2 */
	xpledgex("stdio flock rpath wpath cpath", NULL);

	while ((c =
	    getopt(argc, argv, "qdp:")) != -1) {

		switch (c) {
		case 'd':
			type = MKHTEMP_DIR;
			break;

		case 'p':
			tmpdir = optarg;
			break;

		case 'q': /* don't print errors */
			  /* (exit status unchanged) */
			break;

		default:
			goto err_usage;
		}
	}

	if (optind < argc)
		template = argv[optind];
	if (optind + 1 < argc)
		goto err_usage;

	/* custom template e.g. foo.XXXXXXXXXXXXXXXXXXXXX */
	if (template != NULL) {	
		for (p = template + slen(template, PATH_MAX, &tlen);
		    p > template && *--p == 'X'; xc++);

		if (xc < 3) /* the gnu mktemp errs on less than 3 */
			exitf(
			"template must have 3 X or more on end (12+ advised");
	}

	/* user supplied -p PATH - WARNING:
	 * this permits symlinks, but only here,
	 * not in the library, so they are resolved
	 * here first, and *only here*. the mkhtemp
	 * library blocks them. be careful
	 * when using -p
	 */
	if (tmpdir != NULL) {
		rp = realpath(tmpdir, resolved);
		if (rp == NULL)
			exitf("%s", tmpdir);

		tmpdir = resolved;
	}

	if (new_tmp_common(&fd, &s, type,
	    tmpdir, template) < 0)
		exitf("%s", s);

	xpledgex("stdio", NULL);

	if (s == NULL)
		exitf("bad string initialisation");
	if (*s == '\0')
		exitf("empty string initialisation");

	slen(s, PATH_MAX, &len); /* Nullterminierung prüfen */
	/* for good measure. (bonus: also re-checks length overflow) */

	printf("%s\n", s);

	return EXIT_SUCCESS;

err_usage:
	exitf(
	    "usage: %s [-d] [-p dir] [template]\n", lbgetprogname());
}

static void
exit_cleanup(void)
{
	return;
}
