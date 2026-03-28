/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Hardened mktemp (mkhtemp!)
 *
 * WORK IN PROGRESS (proof of concept), or, v0.0000001
 * DO NOT PUT THIS IN YOUR LINUX DISTRO YET.
 *
 * I will remove this notice when the code is mature, and
 * probably contact several of your projects myself.
 *
 * See README. This is an ongoing project; no proper docs
 * yet, and no manpage (yet!) - the code is documentation,
 * while the specification that it implements evolves.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
/* for openat2 on linux */
#define _GNU_SOURCE 1
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

int
main(int argc, char *argv[])
{
#if defined (PATH_LEN) && \
    (PATH_LEN) >= 256
	size_t maxlen = PATH_LEN;
#else
	size_t maxlen = 4096;
#endif
	size_t len;
	size_t tlen;
	size_t xc = 0;

	char *tmpdir = NULL;
	char *template = NULL;
	char *p;
	char *s = NULL;
	char *rp;
	char resolved[maxlen];
	char c;

	int fd = -1;
	int type = MKHTEMP_FILE;
	int stfu = 0; /* -q option */

	if (lbgetprogname(argv[0]) == NULL)
		err_no_cleanup(stfu, errno, "could not set progname");

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
			stfu = 1;
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
		if (slen(template, maxlen, &tlen) < 0)
			err_no_cleanup(stfu, EINVAL,
			    "invalid template");

		for (p = template + tlen;
		    p > template && *--p == 'X'; xc++);

		if (xc < 3) /* the gnu mktemp errs on less than 3 */
			err_no_cleanup(stfu, EINVAL,
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
			err_no_cleanup(stfu, errno, "%s", tmpdir);

		tmpdir = resolved;
	}

	if (new_tmp_common(&fd, &s, type,
	    tmpdir, template) < 0)
		err_no_cleanup(stfu, errno, "%s", s);

	xpledgex("stdio", NULL);

	if (s == NULL)
		err_no_cleanup(stfu, EFAULT, "bad string initialisation");
	if (*s == '\0')
		err_no_cleanup(stfu, EFAULT, "empty string initialisation");
	if (slen(s, maxlen, &len) < 0)
		err_no_cleanup(stfu, EFAULT, "unterminated string initialisiert");

	printf("%s\n", s);

	return EXIT_SUCCESS;

err_usage:
	err_no_cleanup(stfu, EINVAL,
	    "usage: %s [-d] [-p dir] [template]\n", getnvmprogname());
}/* 

  ( >:3 )    
   /| |\
    / \                                                                      */
