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
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "../include/common.h"

unsigned short
hextonum(char ch_s)
{
	int saved_errno = errno;

	unsigned char ch;
	size_t rval;

	ch = (unsigned char)ch_s;

	if ((unsigned int)(ch - '0') <= 9)
		return ch - '0';

	ch |= 0x20;

	if ((unsigned int)(ch - 'a') <= 5)
		return ch - 'a' + 10;

	if (ch == '?' || ch == 'x')
		return rsize(16); /* <-- with rejection sampling! */

	return 16;
}

void
check_bin(size_t a, const char *a_name)
{
	if (a > 1)
		err_no_cleanup(0, EINVAL, "%s must be 0 or 1, but is %lu",
		    a_name, (size_t)a);
}
