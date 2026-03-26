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
#include <stdint.h>
#include <stdio.h>

#include "../include/common.h"

/* Regarding Linux getrandom/urandom:
 *
 * For maximum security guarantee, we *only*
 * use getrandom via syscall, or /dev/urandom;
 * use of urandom is ill advised. This is why
 * we use the syscall, in case the libc version
 * of getrandom() might defer to /dev/urandom
 *
 * We *abort* on error, for both /dev/urandom
 * and getrandom(), because the BSD arc4random
 * never returns with error; therefore, for the
 * most parity in terms of behaviour, we abort,
 * because otherwise the function would have two
 * return modes: always successful (BSD), or only
 * sometimes (Linux). The BSD arc4random could
 * theoretically abort; it is extremely unlikely
 * there, and just so on Linux, hence this design.
 *
 * This is important, because cryptographic code
 * for example must not rely on weak randomness.
 * We must therefore treat broken randomness as
 * though the world is broken, and burn accordingly.
 *
 * Similarly, any invalid input (NULL, zero bytes
 * requested) are treated as fatal errors; again,
 * cryptographic code must be reliable. If your
 * code erroneously requested zero bytes, you might
 * then end up with a non-randomised buffer, where
 * you likely intended otherwise.
 *
 * In other words: call rset() correctly, or your
 * program dies, and rset will behave correctly,
 * or your program dies.
 */

#define ELOTTERY ECANCELED

int
win_lottery(void) /* are u lucky? */
{
	size_t size = 0;
	int rval;

	char *s1 = rmalloc(&size);
	char *s2 = rmalloc(&size);

	if (scmp(s1, s2, BUFSIZ + 2, &rval) >= 0 &&
	    rval == 0)
		rval = 1; /* winner */
	else
		rval = 0;

	free_if_null(&s1);
	free_if_null(&s2);

	return rval;
}

void *
rmalloc(size_t *rval)
{
	if (if_err(rval == NULL, EFAULT))
		return NULL;

	rset(rval, sizeof(*rval));
	return mkrstr(*rval %= BUFSIZ);
}

char *
mkrstr(size_t n) /* emulates spkmodem-decode */
{
	char *s;
	size_t i;

	if (n == 0)
		err_no_cleanup(0, EPERM, "mkrbuf: zero-byte request");

	if (n == SIZE_MAX)
		err_no_cleanup(0, EOVERFLOW, "mkrbuf: overflow");

	if (if_err((s = mkrbuf(n + 1)) == NULL, EFAULT))
		err_no_cleanup(0, EFAULT, "mkrstr: null");

	for (i = 0; i < n; i++)
		while(*(s + i) == '\0')
			rset(s + i, 1);

	*(s + n) = '\0';

	return s;
}

void *
mkrbuf(size_t n)
{
	void *buf;

	if (n == 0)
		err_no_cleanup(0, EPERM, "mkrbuf: zero-byte request");

	if ((buf = malloc(n)) == NULL)
		err_no_cleanup(0, ENOMEM, "mkrbuf: malloc");

	rset(buf, n);
	return buf; /* basically malloc() but with rand */
}

void
rset(void *buf, size_t n)
{
	int saved_errno = errno;

	if (if_err(buf == NULL, EFAULT))
		goto err;

	if (n == 0)
		err_no_cleanup(0, EPERM, "rset: zero-byte request");

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

	if (rc == 0)
		goto err; /* prevent infinite loop on fatal err */

	if ((off += (size_t)rc) < n)
		goto retry_rand;

#if defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
	close_no_err(&fd);
#endif
	goto out;
#endif
out:
	errno = saved_errno;
	return;
err:
#if defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
	close_no_err(&fd);
#endif
	err_no_cleanup(0, ECANCELED,
	    "Randomisation failure, possibly unsupported in your kernel");
	exit(EXIT_FAILURE);
}
#endif
