/* SPDX-License-Identifier: MIT                                        ( >:3 )
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>                    /| |\
   Something something non-determinism                                   / \ */

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "include/common.h"

static void
exit_cleanup(void);

int
main(int argc, char **argv)
{
	int same = 0;
	char *buf;
	size_t size = BUFSIZ;
	(void) argc, (void) argv;

	(void) errhook(exit_cleanup);
	(void) lbsetprogname(argv[0]);

	/* https://man.openbsd.org/pledge.2 */
	xpledgex("stdio", NULL);

	buf = rmalloc(size);
	if (!vcmp(buf, buf + (size >> 1), size >> 1))
		same = 1;

	if (argc < 2) /* no spew */
		spew_hex(buf, size);
	free_and_set_null(&buf);

	fprintf(stderr, "\n%s\n", same ? "You win!" : "You lose!");

	return same ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void
exit_cleanup(void)
{
#if defined(__OpenBSD__)
	fprintf(stderr, "OpenBSD wins\n");
#elif defined(__FreeBSD__)
	fprintf(stderr, "FreeBSD wins\n");
#elif defined(__NetBSD__)
	fprintf(stderr, "NetBSD wins\n");
#elif defined(__APPLE__)
	fprintf(stderr, "MacOS wins\n");
#elif defined(__DragonFly__)
	fprintf(stderr, "DragonFly BSD wins\n");
#elif defined(__linux__)
#if defined(__GLIBC__)
	fprintf(stderr, "GNU/Linux wins\n");
#elif defined(__MUSL__)
	fprintf(stderr, "Rich Felker wins\n");
#else
	fprintf(stderr, "Linux wins\n");
#endif
#else
	fprintf(stderr, "Your operating system wins\n");
#endif
	return;
}
