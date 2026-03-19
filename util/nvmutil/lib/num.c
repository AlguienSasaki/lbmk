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

#ifdef __OpenBSD__
/* TODO: dead code
	openbsd 2.1 had arc4random.
	arandam was introduced **later**
 */
			if (fd < 0) /* old openbsd */
				fd = open("/dev/arandom",
				    O_RDONLY | O_BINARY | O_NOFOLLOW |
				    O_CLOEXEC);
#endif

#ifdef PORTABLE_RAND_DEV
#if (PORTABLE_RAND_DEV) > 0
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

/* TODO: will re-add timer-based fallback here #if defined(PORTABLE) */

	err(EIO, "Can't read from /dev/[ua]random");
	return 0;

	/* add portable timers here */
#endif
}

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
	} while (ret < 0 && errno == EINTR);

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

void
check_bin(unsigned long a, const char *a_name)
{
	if (a > 1)
		err(EINVAL, "%s must be 0 or 1, but is %lu",
		    a_name, (unsigned long)a);
}
