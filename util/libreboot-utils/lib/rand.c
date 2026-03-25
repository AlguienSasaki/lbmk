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

#ifndef USE_URANDOM
#define USE_URANDOM 0
#endif

#include <errno.h>
#if defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
#include <fcntl.h> /* if not arc4random: /dev/urandom */
#endif

#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "../include/common.h"

void
rset(void *buf, size_t n)
{
	int saved_errno = errno;

#if (defined(__OpenBSD__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__) || \
    defined(__DragonFly__)) && !(defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0))

	arc4random_buf(buf, n);
	goto out;
#else
	size_t off = 0;
	ssize_t rc = 0;

#if defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
	int fd = -1;

	if ((fd = open("/dev/urandom", O_RDONLY)) < 0)
		goto err;
retry_rand:
	if ((rc = read(fd, (unsigned char *)buf + off, n - off)) < 0) {
#elif defined(__linux__)
retry_rand:
	if ((rc = (ssize_t)syscall(SYS_getrandom,
	    (unsigned char *)buf + off, n - off, 0)) < 0) {
#else
#error Unsupported operating system (possibly unsecure randomisation)
#endif
		if (errno == EINTR ||
		    errno == EAGAIN)
			goto retry_rand;

		goto err; /* possibly unsupported by kernel */
	}

	if ((off += (size_t)rc) < n)
		goto retry_rand;

#if defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
	close_no_err(&fd);
#endif

	goto out;
err:
	/* since arc4random always returns
	 * successful, we treat urandom/getrandom
	 * failures as fatal and abort accordingly
	 */
	err_no_cleanup(1, ECANCELED,
	    "Randomisation failure, possibly unsupported in your kernel.");
	exit(EXIT_FAILURE);
#endif

out:
	errno = saved_errno;
}
#endif
