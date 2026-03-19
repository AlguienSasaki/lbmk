/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Numerical functions.
 */

#ifdef __OpenBSD__
#include <sys/param.h>
#endif
#include <sys/types.h>
#if defined(FALLBACK_RAND_1989) && \
    (FALLBACK_RAND_1989) > 0
#include <sys/time.h>
#endif

#include <errno.h>
#if !((defined(__OpenBSD__) && (OpenBSD) >= 201) || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__))
#include <fcntl.h> /* if not arc4random: /dev/urandom */
#endif
#include <limits.h>
#include <stddef.h>
#include <string.h>
#if defined(FALLBACK_RAND_1989) && \
    (FALLBACK_RAND_1989) > 0
#include <time.h>
#endif
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
#if !(defined(FALLBACK_RAND_1989) && \
    ((FALLBACK_RAND_1989) > 0))
#if (defined(__OpenBSD__) && (OpenBSD) >= 201) || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__)

	unsigned long rval;
	arc4random_buf(&rval, sizeof(unsigned long));

	return rval;
#else
	static int fd = -1;
	static long nr = -1;
	static unsigned long off = 0;
#if defined (BUFSIZ)
	static char rbuf[BUFSIZ];
#else
#ifndef PORTABLE
	static char rbuf[4096];
#elif ((PORTABLE) > 0)
	static char rbuf[256]; /* scarce memory on old systems */
#else
	static char rbuf[4096]; /* typical 32-bit BUFSIZ */
#endif
#endif
	unsigned long rval;
	long new_nr;

	int retries = 0;
	int max_retries = 100;

#if defined(__linux__)
#if defined(HAVE_GETRANDOM) || \
    defined(HAVE_GETRANDOM_SYSCALL)

	/* linux getrandom()
	 *
	 * we *can* use arc4random on
	 * modern linux, but not on
	 * every libc. better use the
	 * official linux function
	 *
	 * similar benefits to arc4random
	 * e.g. works in chroot, blocks
	 * until it has enough entropy,
	 * and works even when /dev/urandom
	 * is available (doesn't use it);
	 * it's generally more reliable
	 */

	if (fallback_rand_getrandom(&rval, sizeof(rval)) == 0)
		return rval;

	/*
	 * now fall back to urandom if getrandom failed:
	 */
#endif
#endif

	/* reading from urandom is inherently
	 * unreliable on old systems, even if
	 * newer systems make it more reliable
	 *
	 * modern linux/bsd make it safe, but
	 * we have to assume that someone is
	 * compiling this on linux from 1999
	 *
	 * this logic therefore applies various
	 * tricks to mitigate possible os bugs
	 */

retry_urandom_read:

	if (++retries > max_retries)
		goto rlong_next;

	if (nr < 0 || nr < (long)sizeof(unsigned long)) {

		if (fd < 0) {

			fd = open("/dev/urandom",
			    O_RDONLY | O_BINARY | O_NOFOLLOW |
			    O_CLOEXEC);

#ifdef USE_OLD_DEV_RANDOM
#if (USE_OLD_DEV_RANDOM) > 0
			/* WARNING:
			 *   /dev/random may block
			 *   forever and does **NOT**
			 *   guarantee better entropy
			 *   on old systems
			 *
			 *   only use it if needed
			 */

			if (fd < 0)
				fd = open("/dev/random",
				    O_RDONLY | O_BINARY | O_NOFOLLOW |
				    O_CLOEXEC);
#endif
#endif

			if (fd < 0)
				goto retry_urandom_read;

			retries = 0;
		}

		new_nr = rw_file_exact(fd, (unsigned char *)rbuf,
		    sizeof(rbuf), 0, IO_READ, LOOP_EAGAIN,
		    LOOP_EINTR, MAX_ZERO_RW_RETRY, OFF_ERR);

		if (new_nr < 0 || new_nr < (long)sizeof(rbuf))
			goto retry_urandom_read;

		/* only reset buffer after successful refill */
		nr = new_nr;
		off = 0;

		/* to mitigate file descriptor
		 * injection, we do not re-use
		 * the same descriptor each time
		 */
		(void) close_on_eintr(fd);
		fd = -1;
	}

	fd = -1;
	retries = 0;

	memcpy(&rval, rbuf + off, sizeof(unsigned long));

	nr -= (long)sizeof(unsigned long);
	off += sizeof(unsigned long);

	return rval;

rlong_next:

	fd = -1;
	off = 0;
	nr = -1;

	err(EIO, "Can't read from /dev/[ua]random");
	return 0;

#endif
#else /* FALLBACK_RAND_1989 */
	/* your computer is from a museum
	 */
	unsigned long mix = 0;
	int nr;

	/* 100 times, for entropy
	 */
	for (nr = 0; nr < 100; nr++)
		mix ^= fallback_rand_1989();

	/* 101 times ;)
	 */
	return fallback_rand_1989();
#endif
}

#if !(defined(FALLBACK_RAND_1989) && \
    ((FALLBACK_RAND_1989) > 0))
#if defined(__linux__)
#if defined(HAVE_GETRANDOM) || \
    defined(HAVE_GETRANDOM_SYSCALL)
int
fallback_rand_getrandom(void *buf, size_t len)
{
	ssize_t rval = -1;

	/* keep strict compiler
	 * happy if unused
	 */
	(void)rval;
	(void)buf;
	(void)len;

#if defined(HAVE_GETRANDOM)
	do {
		rval = getrandom(buf, len, 0);
	} while (rval < 0 && errno == EINTR);

#elif defined(HAVE_GETRANDOM_SYSCALL)
	do {
		rval = syscall(SYS_getrandom, buf, len, 0);
	} while (rval < 0 && errno == EINTR);

#else
	return -1;
#endif

#if defined(HAVE_GETRANDOM) || \
    defined(HAVE_GETRANDOM_SYSCALL)

	if (rval == (ssize_t)len) {
		return 0;
	}

	if (rval < 0 && errno == ENOSYS)
		return -1; /* not supported by kernel */

	return -1;
#endif
}
#endif
#endif
#else
/* nobody should use this
 * (not crypto-safe)
 */
unsigned long
fallback_rand_1989(void)
{
	static unsigned long mix = 0;
	static unsigned long counter = 0;

	struct timeval tv;

	gettimeofday(&tv, NULL);

	mix ^= (unsigned long)tv.tv_sec
	    ^ (unsigned long)tv.tv_usec
	    ^ (unsigned long)getpid()
	    ^ (unsigned long)&mix
	    ^ counter++
	    ^ entropy_jitter();

	/*
	 * Stack addresses can vary between
	 * calls, thus increasing entropy.
	 */
	mix ^= (unsigned long)&mix;
	mix ^= (unsigned long)&tv;
	mix ^= (unsigned long)&counter;

	return mix;
}

unsigned long
entropy_jitter(void)
{
	unsigned long mix;

	struct timeval a, b;
	long mix_diff;

	int c;

	mix = 0;

	gettimeofday(&a, NULL);

	for (c = 0; c < 32; c++) {

		getpid();
		gettimeofday(&b, NULL);

		/*
		 * prevent negative numbers to prevent overflow,
		 * which would bias rand to large numbers
		 */
		mix_diff = (long)(b.tv_usec - a.tv_usec);
		if (mix_diff < 0)
			mix_diff = -mix_diff;

		mix ^= (unsigned long)(mix_diff);

		mix ^= (unsigned long)&mix;

	}

	return mix;
}
#endif

void
check_bin(unsigned long a, const char *a_name)
{
	if (a > 1)
		err(EINVAL, "%s must be 0 or 1, but is %lu",
		    a_name, (unsigned long)a);
}
