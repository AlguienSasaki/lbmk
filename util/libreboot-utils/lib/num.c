/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Non-randomisation-related numerical functions.
 * For rand functions, see: rand.c
 */

#ifdef __OpenBSD__
#include <sys/param.h>
#endif
#include <sys/types.h>

#include <errno.h>
#if !((defined(__OpenBSD__) && (OpenBSD) >= 201) || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__))
#include <fcntl.h> /* if not arc4random: /dev/urandom */
#endif
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../include/common.h"

unsigned short
hextonum(char ch_s)
{
	unsigned char ch;

	ch = (unsigned char)ch_s;

	if ((unsigned int)(ch - '0') <= 9)
		return ch - '0';

	ch |= 0x20;

	if ((unsigned int)(ch - 'a') <= 5)
		return ch - 'a' + 10;

	if (ch == '?' || ch == 'x') /* random */
		return (short)rsize(16); /* <-- with rejection sampling! */

	return 16;
}

/* basically hexdump -C */
/*
	TODO: optimise this
	write a full util for hexdump
	how to optimise:
	don't call print tens of thousands of times!
	convert the numbers manually, and cache everything
	in a BUFSIZ sized buffer, with everything properly
	aligned. i worked out that i could fit 79 rows
	in a 8KB buffer (1264 bytes of numbers represented
	as strings in hex)
	this depends on the OS, and would be calculated at
	runtime.
	then:
	don't use printf. just write it to stdout (basically
	a simple cat implementation)
*/
void
spew_hex(const void *data, size_t len)
{
	const unsigned char *buf = (const unsigned char *)data;
	unsigned char c;
	size_t i;
	size_t j;

	if (buf == NULL ||
	    len == 0)
		return;

	for (i = 0; i < len; i += 16) {

		if (len <= 4294967296) /* below 4GB */
			printf("%08zx  ", i);
		else
			printf("%16zu  ", i);

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

void
check_bin(size_t a, const char *a_name)
{
	if (a > 1)
		exitf("%s must be 0 or 1, but is %lu",
		    a_name, (size_t)a);
}
