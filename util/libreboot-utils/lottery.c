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

static void
spew_buf(const void *data, size_t len);

int
main(int argc, char **argv)
{
	int same = 0;
	char *buf;
	(void) argc, (void) argv;

	(void) errhook(exit_cleanup);
	if (lbsetprogname(argv[0]) == NULL)
		err_exit(errno, "could not set progname");

	/* https://man.openbsd.org/pledge.2 */
	xpledgex("stdio", NULL);

	buf = mkrbuf(BUFSIZ);
	if (!memcmp(buf, buf + (BUFSIZ >> 1), BUFSIZ >> 1))
		same = 1;

	if (argc < 2) /* no spew */
		spew_buf(buf, BUFSIZ);
	free(buf);

	fprintf(stderr, "\n%s\n", same ? "You win!" : "You lose!");
	return same ^ 1;
}

static void
spew_buf(const void *data, size_t len)
{
	const unsigned char *buf = data;
	unsigned char c;
	size_t i, j;

	if (buf == NULL ||
	    len == 0)
		return;

	for (i = 0; i < len; i += 16) {

		printf("%08zx  ", i);

		for (j = 0; j < 16; j++) {

			if (i + j < len)
				printf("%02x ", buf[i + j]);
			else
				printf("   ");

			if (j == 7)
				printf(" ");
		}

		printf(" |");

		for (j = 0; j < 16 && i + j < len; j++) {

			c = buf[i + j];
			printf("%c", isprint(c) ? c : '.');
		}

		printf("|\n");
	}

	printf("%08zx\n", len);
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
