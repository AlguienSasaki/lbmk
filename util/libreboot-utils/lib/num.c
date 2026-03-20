/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Numerical functions.
 * NOTE: randomness was moved to rand.c
 */

/*
TODO: properly handle errno in this file
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

/* TODO:
 * make this and errno handling more
 * flexible

  in particular:
  hextonum could be modified to
  write into a buffer instead,
  with the converted numbers,
  of an arbitrary length
 */
unsigned short
hextonum(char ch_s)
{
	int saved_errno = errno;

	/* rlong() can return error,
	   but preserves errno if no
	   error. we need to detect
	   this because it handles
	   /dev/urandom sometimes

	   therefore, if it's zero
	   at start, we know if there
	   was an err at the end, by
	   return value zero, if errno
	   was set; this is technically
	   valid, since zero is also
	   a valid random number!

	it's an edge case that i had
	to fix. i'll rewrite the code
	better later. for now, it
	should be ok.
	 */
	errno = 0;

	unsigned char ch;
	size_t rval;

	ch = (unsigned char)ch_s;

	if ((unsigned int)(ch - '0') <= 9) {

		rval = ch - '0';
		goto hextonum_success;
	}

	ch |= 0x20;

	if ((unsigned int)(ch - 'a') <= 5) {

		rval = ch - 'a' + 10;
		goto hextonum_success;
	}

	if (ch == '?' || ch == 'x') {

		rval = rlong();
		if (errno > 0)
			goto err_hextonum;

		goto hextonum_success;
	}

	goto err_hextonum;

hextonum_success:

	errno = saved_errno;
	return (unsigned short)rval & 0xf;

err_hextonum:

	if (errno == saved_errno)
		errno = EINVAL;
	else
		return 17; /* 17 indicates getrandom/urandom fail */

	return 16; /* invalid character */

	/* caller just checks >15. */
}

void
check_bin(size_t a, const char *a_name)
{
	if (a > 1)
		err_no_cleanup(0, EINVAL, "%s must be 0 or 1, but is %lu",
		    a_name, (size_t)a);
}
