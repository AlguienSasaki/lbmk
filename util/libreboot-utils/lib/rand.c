/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Random number generation
 */

#if defined(USE_ARC4) && \
    ((USE_ARC4) > 0)
#define _DEFAULT_SOURCE 1 /* for arc4random on *linux* */
			/* (not needed on bsd - on bsd,
			   it is used automatically unless
			   overridden with USE_URANDOM */
#elif defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
#include <fcntl.h> /* if not arc4random: /dev/urandom */
#elif defined(__linux__) && \
    !(defined(USE_ARC4) && ((USE_ARC4) > 0))
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <sys/syscall.h>
#include <sys/random.h>
#endif

#ifdef __OpenBSD__
#include <sys/param.h>
#endif
#include <sys/types.h>

#include <errno.h>
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

/* random string generator, with
 * rejection sampling. NOTE: only
 * uses ASCII-safe characters, for
 * printing on a unix terminal
 *
 * you still shouldn't use this for
 * password generation; open diceware
 * passphrases are better for that
 *
 * NOTE: the generated strings must
 * ALSO be safe for file/directory names
 * on unix-like os e.g. linux/bsd
 */
char *
rchars(size_t n) /* emulates spkmodem-decode */
{
	static char ch[] =
	    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	char *s = NULL;
	size_t i;

	smalloc(&s, n + 1);
	for (i = 0; i < n; i++)
		s[i] = ch[rsize(sizeof(ch) - 1)];

	*(s + n) = '\0';
	return s;
}

size_t
rsize(size_t n)
{
	size_t rval = SIZE_MAX;
	if (!n)
		exitf("rsize: division by zero");

	/* rejection sampling (clamp rand to eliminate modulo bias) */
	for (; rval >= SIZE_MAX - (SIZE_MAX % n); rset(&rval, sizeof(rval)));

	return rval % n;
}

void *
rmalloc(size_t n)
{
	void *buf = NULL;
	rset(vmalloc(&buf, n), n);
	return buf; /* basically malloc() but with rand */
}

void
rset(void *buf, size_t n)
{
	int saved_errno = errno;
	errno = 0;

	if (if_err(buf == NULL, EFAULT))
		goto err;

	if (n == 0)
		exitf("rset: zero-byte request");

/* on linux, getrandom is recommended,
   but you can pass -DUSE_ARC4=1 to use arc4random.
   useful for portability testing from linux.
 */
#if (defined(USE_ARC4) && ((USE_ARC4) > 0)) || \
    ((defined(__OpenBSD__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__) || \
    defined(__DragonFly__)) && !(defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)))

	arc4random_buf(buf, n);
#else
	size_t off = 0;

retry_rand: {

#if defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
	ssize_t rval;
	int fd = -1;

	open_file_on_eintr("/dev/urandom", &fd, O_RDONLY, 0400, NULL);

	while (rw_retry(saved_errno,
	    rval = rw(fd, (unsigned char *)buf + off, n - off, 0, IO_READ)));
#elif defined(__linux__)
	long rval;
	while (sys_retry(saved_errno,
		rval = syscall(SYS_getrandom,
		    (unsigned char *)buf + off, n - off, 0)));
#else
#error Unsupported operating system (possibly unsecure randomisation)
#endif

	if (rval < 0 || /* syscall fehler */
	    rval == 0) { /* prevent infinite loop on fatal err */
#if defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
		xclose(&fd);
#endif
		goto err;
	}

	if ((off += (size_t)rval) < n)
		goto retry_rand;

#if defined(USE_URANDOM) && \
    ((USE_URANDOM) > 0)
	xclose(&fd);
#endif
}

#endif
	reset_caller_errno(0);
	return;
err:
	(void) with_fallback_errno(ECANCELED);
	exitf("Randomisierungsfehler");
	exit(EXIT_FAILURE);
}
