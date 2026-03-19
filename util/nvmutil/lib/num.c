/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Numerical functions.
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

	if (ch == '?' || ch == 'x')
		return (unsigned short)rlong() & 0xf;

	return 16; /* invalid character */
}

/* Random numbers
 */

unsigned long
rlong(void)
{
#if (defined(__OpenBSD__) && (OpenBSD) >= 201) || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__)

	unsigned long rval;
	arc4random_buf(&rval, sizeof(unsigned long));

	return rval;
#else
	int fd;

	long nr;

	unsigned long rval;

	fd = open("/dev/urandom", O_RDONLY | O_BINARY);

#ifdef __OpenBSD__
	if (fd < 0) /* old openbsd */
		fd = open("/dev/arandom", O_RDONLY | O_BINARY);
#endif

	if (fd < 0)
		fd = open("/dev/random", O_RDONLY | O_BINARY);

	if (fd < 0)
		err(errno, "can't open random device");

	nr = rw_file_exact(fd, (unsigned char *)&rval,
	    sizeof(unsigned long), 0, IO_READ, LOOP_EAGAIN,
	    LOOP_EINTR, MAX_ZERO_RW_RETRY, OFF_ERR);

	if (close_on_eintr(fd) < 0)
		err(errno, "Can't close randomness fd");

	if (nr != sizeof(unsigned long))
		err(errno, "Incomplete read from random device");

	return rval;
#endif
}

void
check_bin(unsigned long a, const char *a_name)
{
	if (a > 1)
		err(EINVAL, "%s must be 0 or 1, but is %lu",
		    a_name, (unsigned long)a);
}
