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
	xpledgex("stdio", NULL);

	(void) errhook(exit_cleanup);

	buf = mkrbuf(BUFSIZ + 1);
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
	return;
}
