/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * WORK IN PROGRESS (proof of concept), or, v0.0000001
 *
 * Mkhtemp - Hardened mktemp. Create files and directories
 * randomly as determined by user's TMPDIR, or fallback. It
 * attemps to provide mitigation against several TOCTOU-based
 * attacks e.g. directory rename / symlink attacks, and it
 * generally provides much higher strictness than previous
 * implementations such as mktemp, mkstemp or even mkdtemp.
 *
 * It uses several modern features by default, e.g. openat2
 * and O_TMPFILE on Linux, with additional hardening; BSD
 * projects only have openat so the code uses that there.
 *
 * Many programs rely on mktemp, and they use TMPDIR in a way
 * that is quite insecure. Mkhtemp intends to change that,
 * quite dramatically, with: userspace sandbox (and use OS
 * level options e.g. OBSD pledge where available), constant
 * identity/ownership checks on files, MUCH stricter ownership
 * restrictions (e.g. enforce sticky bit policy on world-
 * writeable tmpdirs), preventing operation on other people's
 * files (only your own files) - even root is restricted,
 * depending on how the code is compiled. Please read the code.
 *
 * This is the utility version, which makes use of the also-
 * included library. No docs yet - source code are the docs,
 * and the (ever evolving, and hardening) specification.
 *
 * This was written from scratch, for use in nvmutil, and
 * it is designed to be portable (BSD, Linux). Patches
 * very much welcome.
 *
 * WARNING: This is MUCH stricter than every other mktemp
 *          implementation, even more so than mkdtemp or
 *          the OpenBSD version of mkstemp. It *will* break,
 *	    or more specifically, reveal the flaws in, almost
 *	    every major critical infrastructure, because most
 *	    people already use mktemp extremely insecurely.
 *
 * This tool is written by me, for me, and also Libreboot, but
 * it will be summitted for review to various Linux distros
 * and BSD projects once it has reached maturity.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
/* for openat2 on linux */
#define _GNU_SOURCE 1
#endif

#ifdef __OpenBSD__
#include <sys/param.h> /* pledge(2) */
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
#if defined(__OpenBSD__) && defined(OpenBSD)
#if (OpenBSD) >= 509
	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		goto err_usage;
#endif
#endif

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

#if defined(__OpenBSD__) && defined(OpenBSD)
#if (OpenBSD) >= 509
	if (pledge("stdio", NULL) == -1)
		err_no_cleanup(stfu, errno, "pledge, exit");
#endif
#endif

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
    / \





                                                                             */












