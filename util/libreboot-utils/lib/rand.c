/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Random number generation
 */

#ifndef RAND_H
#define RAND_H

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
#include <stdlib.h>

#include "../include/common.h"

/* Random numbers
 */

/* when calling this: save errno
 * first, then set errno to zero.
 * on error, this function will
 * set errno and possibly return
 *
 * rlong also preserves errno
 * and leaves it unchanged on
 * success, so if you do it
 * right, you can detect error.
 * this is because it uses
 * /dev/urandom which can err.
 * ditto getrandom (EINTR),
 * theoretically.
 */

/* for the linux version: we use only the
 * syscall, because we cannot trust /dev/urandom
 * to be as robust, and some libc implementations
 * may default to /dev/urandom under fault conditions.
 *
 * for general high reliability, we must abort on
 * failure. in practise, it will likely never fail.
 * the arc4random call on bsd never returns error.
 */

size_t
rlong(void)
{
	size_t rval;
	int saved_errno = errno;
	errno = 0;

#if (defined(__OpenBSD__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__) || \
    defined(__DragonFly__))

	arc4random_buf(&rval, sizeof(size_t));
	goto out;

#elif defined(__linux__)

	size_t off = 0;
	size_t len = sizeof(rval);
	ssize_t rc;

retry_rand:
	rc = (ssize_t)syscall(SYS_getrandom,
	    (char *)&rval + off, len - off, 0);

	if (rc < 0) {
		if (errno == EINTR || errno == EAGAIN) {
			usleep(100);
			goto retry_rand;
		}

		goto err; /* possibly unsupported by kernel */
	}

	if ((off += (size_t)rc) < len)
		goto retry_rand;

	goto out;
err:
	/*
	 * getrandom can return with error, but arc4random
	 * doesn't. generally, getrandom will be reliable,
	 * but we of course have to maintain parity with
	 * BSD. So a rand failure is to be interpreted as
	 * a major systems failure, and we act accordingly.
	 */
	err_no_cleanup(1, ECANCELED,
	    "Randomisation failure, possibly unsupported in your kernel.");
	exit(EXIT_FAILURE);

#else
#error Unsupported operating system (possibly unsecure randomisation)
#endif

out:
	errno = saved_errno;
	return rval;
}
#endif
