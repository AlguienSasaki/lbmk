/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * String functions
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>

#include "../include/common.h"

/* for null detection inside
 * word-optimised string functions
 */
#define ff ((size_t)-1 / 0xFF)
#define high ((ff) * 0x80)
/* NOTE:
 * do not assume that a match means
 * both words have null at the same
 * location. see how this is handled
 * e.g. in scmp.
 */
#define zeroes(x) (((x) - (ff)) & ~(x) & (high))

size_t
page_remain(const void *p)
{
	/* calling sysconf repeatedly
	 * is folly. cache it (static)
	 */
	static size_t pagesz = 0;
	if (!pagesz)
		pagesz = (size_t)pagesize();

	return pagesz - ((uintptr_t)p & (pagesz - 1));
}

long
pagesize(void)
{
	static long rval = 0;
	static int set = 0;
	int saved_errno = 0;

	if (!set) {
		if ((rval = sysconf(_SC_PAGESIZE)) < 0)
			exitf("could not determine page size");
		set = 1;
	}

	reset_caller_errno(0);
	return rval;
}

void
free_and_set_null(char **buf)
{
	if (buf == NULL)
		exitf(
		    "null ptr (to ptr for freeing) in free_and_set_null");

	if (*buf == NULL)
		return;

	free(*buf);
	*buf = NULL;
}

/* safe(ish) malloc.

   use this and free_and_set_null()
   in your program, to reduce the
   chance of use after frees!

   if you use these functions in the
   intended way, you will greatly reduce
   the number of bugs in your code
 */
char *
smalloc(char **buf, size_t size)
{
	return (char *)vmalloc((void **)buf, size);
}
void *
vmalloc(void **buf, size_t size)
{
	int saved_errno = errno;
	void *rval = NULL;
	errno = 0;

	if (size >= SIZE_MAX - 1)
		exitf("integer overflow in vmalloc");
	if (buf == NULL)
		exitf("Bad pointer passed to vmalloc");

	/* lots of programs will
	 * re-initialise a buffer
	 * that was allocated, without
	 * freeing or NULLing it. this
	 * is here intentionally, to
	 * force the programmer to behave
	 */
	if (*buf != NULL)
		exitf("Non-null pointer given to vmalloc");

	if (!size)
		exitf(
		   "Tried to vmalloc(0) and that is very bad. Fix it now");

	if ((rval = malloc(size)) == NULL)
		exitf("malloc fail in vmalloc");

	reset_caller_errno(0);
	return *buf = rval;
}

/* strict word-based strcmp */
int
scmp(const char *a,
    const char *b,
    size_t maxlen,
    int *rval)
{
	size_t i = 0;
	size_t j;
	size_t wa;
	size_t wb;
	int saved_errno = errno;
	errno = 0;

	if (if_err(a == NULL || b == NULL || rval == NULL, EFAULT))
		goto err;

	for ( ; ((uintptr_t)(a + i) % sizeof(size_t)) != 0; i++) {

		if (if_err(i >= maxlen, EOVERFLOW))
			goto err;
		else if (!ccmp(a, b, i, rval))
			goto out;
	}

	for ( ; i + sizeof(size_t) <= maxlen;
	    i += sizeof(size_t)) {

		/* prevent crossing page boundary on word check */
		if (page_remain(a + i) < sizeof(size_t) ||
		    page_remain(b + i) < sizeof(size_t))
			break;

		memcpy(&wa, a + i, sizeof(size_t));
		memcpy(&wb, b + i, sizeof(size_t));

		if (wa != wb)
			for (j = 0; j < sizeof(size_t); j++)
				if (!ccmp(a, b, i + j, rval))
					goto out;

		if (!zeroes(wa))
			continue;

		*rval = 0;
		goto out;
	}

	for ( ; i < maxlen; i++)
		if (!ccmp(a, b, i, rval))
			goto out;

err:
	(void) with_fallback_errno(EFAULT);
	if (rval != NULL)
		*rval = -1;

	exitf("scmp");
	return -1;
out:
	reset_caller_errno(0);
	return *rval;
}

int ccmp(const char *a, const char *b,
    size_t i, int *rval)
{
	unsigned char ac;
	unsigned char bc;

	if (if_err(a == NULL || b == NULL || rval == NULL, EFAULT))
		exitf("ccmp");

	ac = (unsigned char)a[i];
	bc = (unsigned char)b[i];

	if (ac != bc) {
		*rval = ac - bc;
		return 0;
	} else if (ac == '\0') {
		*rval = 0;
		return 0;
	}

	return 1;
}

/* strict word-based strlen */
size_t
slen(const char *s,
    size_t maxlen,
    size_t *rval)
{
	int saved_errno = errno;
	size_t i = 0;
	size_t w;
	size_t j;
	errno = 0;

	if (if_err(s == NULL || rval == NULL, EFAULT))
		goto err;

	for ( ; ((uintptr_t)(s + i) % sizeof(size_t)) != 0; i++) {

		if (if_err(i >= maxlen, EOVERFLOW))
			goto err;
		if (s[i] == '\0') {
			*rval = i;
			goto out;
		}
	}

	for ( ; i + sizeof(size_t) <= maxlen;
	    i += sizeof(size_t)) {

		memcpy(&w, s + i, sizeof(size_t));
		if (!zeroes(w))
			continue;

		for (j = 0; j < sizeof(size_t); j++) {
			if (s[i + j] == '\0') {
				*rval = i + j;
				goto out;
			}
		}
	}

	for ( ; i < maxlen; i++) {
		if (s[i] == '\0') {
			*rval = i;
			goto out;
		}
	}

err:
	(void) with_fallback_errno(EFAULT);
	if (rval != NULL)
		*rval = 0;

	exitf("slen"); /* abort */
	return 0; /* gcc15 is happy */
out:
	reset_caller_errno(0);
	return *rval;
}

/* strict word-based strdup */
char *
sdup(const char *s,
    size_t max, char **dest)
{
	size_t j;
	size_t w;
	size_t i = 0;
	char *out = NULL;
	int saved_errno = errno;
	errno = 0;

	if (if_err(dest == NULL || *dest != NULL || s == NULL, EFAULT))
		goto err;

	out = smalloc(dest, max);

	for ( ; ((uintptr_t)(s + i) % sizeof(size_t)) != 0; i++) {

		if (if_err(i >= max, EOVERFLOW))
			goto err;

		out[i] = s[i];
		if (s[i] == '\0') {
			*dest = out;
			goto out;
		}
	}

	for ( ; i + sizeof(size_t) <= max; i += sizeof(size_t)) {

		if (page_remain(s + i) < sizeof(size_t))
			break;

		memcpy(&w, s + i, sizeof(size_t));
		if (!zeroes(w)) {
			memcpy(out + i, &w, sizeof(size_t));
			continue;
		}

		for (j = 0; j < sizeof(size_t); j++) {

			out[i + j] = s[i + j];
			if (s[i + j] == '\0') {
				*dest = out;
				goto out;
			}
		}
	}

	for ( ; i < max; i++) {

		out[i] = s[i];
		if (s[i] == '\0') {
			*dest = out;
			goto out;
		}
	}

err:
	free_and_set_null(&out);
	if (dest != NULL)
		*dest = NULL;

	(void) with_fallback_errno(EFAULT);
	exitf("sdup");

	return NULL;
out:
	reset_caller_errno(0);
	return *dest;
}

/* concatenate N number of strings */
char *
scatn(ssize_t sc, const char **sv,
    size_t max, char **rval)
{
	int saved_errno = errno;
	char *final = NULL;
	char *rcur = NULL;
	char *rtmp = NULL;
	ssize_t i;
	errno = 0;

	if (if_err(sc < 2, EINVAL) ||
	    if_err(sv == NULL, EFAULT) ||
	    if_err(rval == NULL || *rval != NULL, EFAULT))
		goto err;

	for (i = 0; i < sc; i++) {

		if (if_err(sv[i] == NULL, EFAULT))
			goto err;
		else if (i == 0) {
			(void) sdup(sv[0], max, &final);
			continue;
		}

		rtmp = NULL;
		scat(final, sv[i], max, &rtmp);

		free_and_set_null(&final);
		final = rtmp;
		rtmp = NULL;
	}

	reset_caller_errno(0);
	*rval = final;
	return *rval;
err:
	free_and_set_null(&rcur);
	free_and_set_null(&rtmp);
	free_and_set_null(&final);

	(void) with_fallback_errno(EFAULT);

	exitf("scatn");
	return NULL;
}

/* strict strcat */
char *
scat(const char *s1, const char *s2,
    size_t n, char **dest)
{
	size_t size1;
	size_t size2;
	char *rval = NULL;
	int saved_errno = errno;
	errno = 0;

	if (if_err(dest == NULL || *dest != NULL, EFAULT))
		goto err;

	slen(s1, n, &size1);
	slen(s2, n, &size2);

	if (if_err(size1
	    > SIZE_MAX - size2 - 1, EOVERFLOW))
		goto err;

	smalloc(&rval, size1 + size2 + 1);

	memcpy(rval, s1, size1);
	memcpy(rval + size1, s2, size2);
	*(rval + size1 + size2) = '\0';

	reset_caller_errno(0);
	*dest = rval;
	return *dest;
err:
	(void) with_fallback_errno(EINVAL);
	if (dest != NULL)
		*dest = NULL;
	exitf("scat");

	return NULL;
}

/* strict split/de-cat - off is where
   2nd buffer will start from */
void
dcat(const char *s, size_t n,
    size_t off, char **dest1,
    char **dest2)
{
	size_t size;
	char *rval1 = NULL;
	char *rval2 = NULL;
	int saved_errno = errno;
	errno = 0;

	if (if_err(dest1 == NULL || dest2 == NULL, EFAULT))
		goto err;

	if (if_err(slen(s, n, &size) >= SIZE_MAX - 1, EOVERFLOW) ||
	    if_err(off >= size, EOVERFLOW))
		goto err;

	memcpy(smalloc(&rval1, off + 1),
	    s, off);
	*(rval1 + off) = '\0';

	memcpy(smalloc(&rval2, size - off +1),
	    s + off, size - off);
	*(rval2 + size - off) = '\0';

	*dest1 = rval1;
	*dest2 = rval2;

	reset_caller_errno(0);
	return;

err:
	*dest1 = *dest2 = NULL;

	free_and_set_null(&rval1);
	free_and_set_null(&rval2);

	(void) with_fallback_errno(EINVAL);
	exitf("dcat");
}

/* because no libc reimagination is complete
 * without a reimplementation of memcmp. and
 * no safe one is complete without null checks.
 */
int
vcmp(const void *s1, const void *s2, size_t n)
{
	int saved_errno = errno;
	size_t i = 0;
	size_t a;
	size_t b;

	const unsigned char *x;
	const unsigned char *y;
	errno = 0;

	if (if_err(s1 == NULL || s2 == NULL, EFAULT))
		exitf("vcmp: null input");

	x = s1;
	y = s2;

	for ( ; i + sizeof(size_t) <= n; i += sizeof(size_t)) {

		memcpy(&a, x + i, sizeof(size_t));
		memcpy(&b, y + i, sizeof(size_t));

		if (a != b)
			break;
	}

	for ( ; i < n; i++)
		if (x[i] != y[i])
			return (int)x[i] - (int)y[i];

	reset_caller_errno(0);
	return 0;
}

/* on functions that return with errno,
 * i sometimes have a default fallback,
 * which is set if errno wasn't changed,
 * under error condition.
 */
int
with_fallback_errno(int fallback)
{
	if (!errno)
		errno = fallback;
	return -1;
}

/* the one for nvmutil state is in state.c */
/* this one just exits */
void
exitf(const char *msg, ...)
{
	va_list args;
	int saved_errno = errno;

	func_t err_cleanup = errhook(NULL);
	err_cleanup();
	reset_caller_errno(0);
	saved_errno = errno;

	if (!errno)
		saved_errno = errno = ECANCELED;

	fprintf(stderr, "%s: ", lbgetprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	errno = saved_errno;
	fprintf(stderr, ": %s\n", strerror(errno));

	exit(EXIT_FAILURE);
}

/* the err function will
 * call this upon exit, and
 * cleanup will be performed
 * e.g. you might want to
 * close some files, depending
 * on your program.
 * see: exitf()
 */
func_t errhook(func_t ptr)
{
	static int set = 0;
	static func_t hook = NULL;

	if (!set) {
		set = 1;

		if (ptr == NULL)
			hook = no_op;
		else
			hook = ptr;
	}

	return hook;
}

void
no_op(void)
{
	return;
}

const char *
lbgetprogname(void)
{
	char *name = lbsetprogname(NULL);
	char *p = NULL;
	if (name)
		p = strrchr(name, '/');
	if (p)
		return p + 1;
	else if (name)
		return name;
	else
		return "libreboot-utils";
}

/* singleton. if string not null,
   sets the string. after set,
   will not set anymore. either
   way, returns the string
 */
char *
lbsetprogname(char *argv0)
{
	static char *progname = NULL;
	static int set = 0;

	if (!set) {
		if (argv0 == NULL)
			return "libreboot-utils";
		(void) sdup(argv0, PATH_MAX, &progname);
		set = 1;
	}

	return progname;
}

/* https://man.openbsd.org/pledge.2
   https://man.openbsd.org/unveil.2     */
int
xpledgex(const char *promises, const char *execpromises)
{
	int saved_errno = errno;
	(void) promises, (void) execpromises, (void) saved_errno;
	errno = 0;
#ifdef __OpenBSD__
	if (pledge(promises, execpromises) == -1)
		exitf("pledge");
#endif
	reset_caller_errno(0);
	return 0;
}
int
xunveilx(const char *path, const char *permissions)
{
	int saved_errno = errno;
	(void) path, (void) permissions, (void) saved_errno;
	errno = 0;
#ifdef __OpenBSD__
	if (pledge(promises, execpromises) == -1)
		exitf("pledge");
#endif
	reset_caller_errno(0);
	return 0;
}
