/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * String functions
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#include "../include/common.h"

/* Portable strncmp() that blocks
 * NULL/empty/unterminated strings
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

	err(EINVAL, "Unterminated string in xstrxcmp");

	errno = EINVAL;
	return -1;
}

/* Portable strncmp() that blocks
 * NULL/empty/unterminated strings
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
