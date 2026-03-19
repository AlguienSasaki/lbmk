/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * String handling.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#include "../include/common.h"

/*
 * Portable strcmp() but blocks NULL/empty/unterminated
 * strings. Even stricter than strncmp().
 */
int
xstrxcmp(const char *a, const char *b, unsigned long maxlen)
{
	unsigned long i;

	if (a == NULL || b == NULL)
		err(EINVAL, "NULL input to xstrxcmp");

	if (*a == '\0' || *b == '\0')
		err(EINVAL, "Empty string in xstrxcmp");

	for (i = 0; i < maxlen; i++) {

		unsigned char ac = (unsigned char)a[i];
		unsigned char bc = (unsigned char)b[i];

		if (ac == '\0' || bc == '\0') {
			if (ac == bc)
				return 0;
			return ac - bc;
		}

		if (ac != bc)
			return ac - bc;
	}

	/*
	 * We reached maxlen, so assume unterminated string.
	 */
	err(EINVAL, "Unterminated string in xstrxcmp");

	/*
	 * Should never reach here. This keeps compilers happy.
	 */
	errno = EINVAL;
	return -1;
}

/*
 * strnlen() but aborts on NULL input, and empty strings.
 * Our version also prohibits unterminated strings.
 * strnlen() was standardized in POSIX.1-2008 and is not
 * available on some older systems, so we provide our own.
 */
unsigned long
xstrxlen(const char *scmp, unsigned long maxlen)
{
	unsigned long xstr_index;

	if (scmp == NULL)
		err(EINVAL, "NULL input to xstrxlen");

	if (*scmp == '\0')
		err(EINVAL, "Empty string in xstrxlen");

	for (xstr_index = 0;
	    xstr_index < maxlen && scmp[xstr_index] != '\0';
	    xstr_index++);

	if (xstr_index == maxlen)
		err(EINVAL, "Unterminated string in xstrxlen");

	return xstr_index;
}

char *
x_c_strrchr(const char *s, int c)
{
	const char *p = NULL;

	for ( ; *s; s++)
		if (*s == (char)c)
			p = s;

	if (c == '\0')
		return (char *)s;

	return (char *)p;
}

void *
x_v_memcpy(void *dst, const void *src, unsigned long n)
{
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;

	while (n--)
		*d++ = *s++;

	return dst;
}

int
x_i_memcmp(const void *a, const void *b, unsigned long n)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;

	for ( ; n--; ++pa, ++pb)
		if (*pa != *pb)
			return *pa - *pb;

	return 0;
}
