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

void
free_and_set_null(char **buf)
{
	if (buf == NULL)
		err_exit(EFAULT,
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
	void *rval = NULL;

	if (size >= SIZE_MAX - 1)
		err_exit(EOVERFLOW, "integer overflow in vmalloc");
	if (buf == NULL)
		err_exit(EFAULT, "Bad pointer passed to vmalloc");

	/* lots of programs will
	 * re-initialise a buffer
	 * that was allocated, without
	 * freeing or NULLing it. this
	 * is here intentionally, to
	 * force the programmer to behave
	 */
	if (*buf != NULL)
		err_exit(EFAULT, "Non-null pointer given to vmalloc");

	if (!size)
		err_exit(EFAULT,
		   "Tried to vmalloc(0) and that is very bad. Fix it now");

	if ((rval = malloc(size)) == NULL)
		err_exit(errno, "malloc fail in vmalloc");

	return *buf = rval;
}

/* strict strcmp */
int
scmp(const char *a,
    const char *b,
    size_t maxlen,
    int *rval)
{
	size_t ch;
	unsigned char ac;
	unsigned char bc;

	if (a == NULL ||
	    b == NULL ||
	    rval == NULL) {
		errno = EFAULT;
		goto err;
	}

	for (ch = 0; ch < maxlen; ch++) {

		ac = (unsigned char)a[ch];
		bc = (unsigned char)b[ch];

		if (ac != bc) {
			*rval = ac - bc;
			return 0;
		}

		if (ac == '\0') {
			*rval = 0;
			return 0;
		}
	}

err:
	errno = EFAULT;
	if (rval != NULL)
		*rval = -1;
	return -1;
}

/* strict strlen */
int
slen(const char *s,
    size_t maxlen,
    size_t *rval)
{
	size_t ch;

	if (s == NULL ||
	    rval == NULL) {
		errno = EFAULT;
		goto err;
	}

	for (ch = 0;
	    ch < maxlen && s[ch] != '\0';
	    ch++);

	if (ch == maxlen) {
		/* unterminated */
		errno = EFAULT;
		goto err;
	}

	*rval = ch;
	return 0;
err:
	if (rval != NULL)
		*rval = 0;
	return -1;
}

/* strict strdup */
int
sdup(const char *s,
    size_t n, char **dest)
{
	size_t size;
	char *rval = NULL;

	if (dest == NULL ||
	    slen(s, n, &size) < 0) {
		if (dest != NULL)
			*dest = NULL;
		return -1;
	}

	memcpy(smalloc(&rval, size + 1), s, size);
	*(rval + size) = '\0';

	*dest = rval;
	return 0;
}

/* concatenate N number of strings */
int
scatn(ssize_t sc, const char **sv,
    size_t max, char **rval)
{
	int saved_errno = errno;
	char *final = NULL;
	char *rcur = NULL;
	char *rtmp = NULL;
	size_t i;

	if (if_err(sc < 2, EINVAL) ||
	    if_err(sv == NULL, EFAULT) ||
	    if_err(rval == NULL || *rval != NULL, EFAULT))
		goto err;

	for (i = 0; i < sc; i++) {

		if (if_err(sv[i] == NULL, EFAULT))
			goto err;
		else if (i == 0) {
			if (sdup(sv[0], max, &final) < 0)
				goto err;
			continue;
		}

		rtmp = NULL;
		if (scat(final, sv[i], max, &rtmp) < 0)
			goto err;

		free_and_set_null(&final);
		final = rtmp;
		rtmp = NULL;
	}

	*rval = final;
	return 0;
err:
	free_and_set_null(&rcur);
	free_and_set_null(&rtmp);
	free_and_set_null(&final);

	return set_errno(saved_errno, EFAULT);
}

/* strict strcat */
int
scat(const char *s1, const char *s2,
    size_t n, char **dest)
{
	size_t size1;
	size_t size2;
	char *rval = NULL;

	if (if_err(dest == NULL || *dest != NULL, EFAULT) ||
	    slen(s1, n, &size1) < 0 ||
	    slen(s2, n, &size2) < 0 ||
	    if_err(size1 > SIZE_MAX - size2 - 1, EOVERFLOW)) {

		if (dest != NULL)
			*dest = NULL;
		return -1;
	}

	memcpy(smalloc(&rval, size1 + size2 + 1),
	    s1, size1);
	memcpy(rval + size1, s2, size2);
	*(rval + size1 + size2) = '\0';

	*dest = rval;
	return 0;
}

/* strict split/de-cat - off is where
   2nd buffer will start from */
int
dcat(const char *s, size_t n,
    size_t off, char **dest1,
    char **dest2)
{
	size_t size;
	char *rval1 = NULL;
	char *rval2 = NULL;

	if (dest1 == NULL || dest2 == NULL ||
	    slen(s, n, &size) < 0 ||
	    if_err(size == SIZE_MAX, EOVERFLOW) ||
	    if_err(off >= size, EOVERFLOW)) {

		goto err;
	}

	memcpy(smalloc(&rval1, off + 1),
	    s, off);
	*(rval1 + off) = '\0';

	memcpy(smalloc(&rval2, size - off +1),
	    s + off, size - off);
	*(rval2 + size - off) = '\0';

	*dest1 = rval1;
	*dest2 = rval2;

	return 0;

err:
	if (rval1 != NULL)
		free(rval1);
	if (rval2 != NULL)
		free(rval2);

	if (dest1 != NULL)
		*dest1 = NULL;
	if (dest2 != NULL)
		*dest2 = NULL;

	return -1;
}

/* because no libc reimagination is complete
 * without a reimplementation of memcmp. and
 * no safe one is complete without null checks.
 */
int
vcmp(const void *s1, const void *s2, size_t n)
{
	size_t i = 0;
	size_t a;
	size_t b;

	if (if_err(s1 == NULL || s2 == NULL, EFAULT))
		err_exit(EFAULT, "vcmp: null input");

	const unsigned char *x = s1;
	const unsigned char *y = s2;

	for ( ; i + sizeof(size_t) <= n; i += sizeof(size_t)) {

		memcpy(&a, x + i, sizeof(size_t));
		memcpy(&b, y + i, sizeof(size_t));

		if (a != b)
			break;
	}

	for ( ; i < n; i++)
		if (x[i] != y[i])
			return (int)x[i] - (int)y[i];

	return 0;
}

/* on functions that return with errno,
 * i sometimes have a default fallback,
 * which is set if errno wasn't changed,
 * under error condition.
 */
int
set_errno(int saved_errno, int fallback)
{
	if (errno == saved_errno)
		errno = fallback;
	return -1;
}

/* the one for nvmutil state is in state.c */
/* this one just exits */
void
err_exit(int nvm_errval, const char *msg, ...)
{
	va_list args;
	int saved_errno = errno;
	const char *p;

	func_t err_cleanup = errhook(NULL);
	err_cleanup();
	errno = saved_errno;

	if (!errno)
		saved_errno = errno = ECANCELED;

	fprintf(stderr, "%s: ", lbgetprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	fprintf(stderr, ": %s\n", strerror(errno));

	exit(EXIT_FAILURE);
}

/* the err function will
 * call this upon exit, and
 * cleanup will be performed
 * e.g. you might want to
 * close some files, depending
 * on your program.
 * see: err_exit()
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
		if (argv0 == NULL || sdup(argv0, 4096, &progname) < 0)
			return "libreboot-utils";
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
#ifdef __OpenBSD__
	if (pledge(promises, execpromises) == -1)
		err_exit(errno, "pledge");
#endif
	errno = saved_errno;
	return 0;
}
int
xunveilx(const char *path, const char *permissions)
{
	int saved_errno = errno;
	(void) path, (void) permissions, (void) saved_errno;
#ifdef __OpenBSD__
	if (pledge(promises, execpromises) == -1)
		err_exit(errno, "pledge");
#endif
	errno = saved_errno;
	return 0;
}
