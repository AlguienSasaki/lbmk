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
		err_no_cleanup(0, EOVERFLOW, "integer overflow in vmalloc");
	if (buf == NULL)
		err_no_cleanup(0, EFAULT, "Bad pointer passed to vmalloc");

	/* lots of programs will
	 * re-initialise a buffer
	 * that was allocated, without
	 * freeing or NULLing it. this
	 * is here intentionally, to
	 * force the programmer to behave
	 */
	if (*buf != NULL)
		err_no_cleanup(0, EFAULT, "Non-null pointer given to vmalloc");

	if (!size)
		err_no_cleanup(0, EFAULT,
		   "Tried to vmalloc(0) and that is very bad. Fix it now");

	if ((rval = malloc(size)) == NULL)
		err_no_cleanup(0, errno, "malloc fail in vmalloc");

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
/* slen already checks null/termination */
int
scatn(ssize_t sc, const char **sv,
    size_t max, char **rval)
{
	ssize_t i = 0;

	size_t ts = 0;
	size_t *size = NULL;

	char *ct = NULL;
	int saved_errno = errno;

	if (if_err(sc <= 0, EINVAL) ||
	    if_err(sc > SIZE_MAX / sizeof(size_t), EOVERFLOW) ||
	    if_err(sv == NULL, EINVAL))
		goto err;

	vmalloc((void **)&size, sizeof(size_t) * sc);

	for (i = 0; i < sc; i++, ts += size[i])
		if (if_err(sv[i] == NULL, EINVAL) ||
		    slen(sv[i], max, &size[i]) < 0 ||
		    if_err(size[i] > max - 1, EOVERFLOW) ||
		    if_err((size[i] + ts) < ts, EOVERFLOW))
			goto err;

	if (if_err(ts > SIZE_MAX - 1, EOVERFLOW) ||
	    if_err(ts > max - 1, EOVERFLOW))
		goto err;

	smalloc(&ct, ts + 1);
	for (ts = i = 0; i < sc; i++, ts += size[i])
		memcpy(ct + ts, sv[i], size[i]);

	*(ct + ts) = '\0';
	*rval = ct;

	errno = saved_errno;
	return 0;
err:
	if (ct != NULL)
		free(ct);
	if (size != NULL)
		free(size);
	if (errno == saved_errno)
		errno = EFAULT;

	return -1;
}

/* strict strcat */
int
scat(const char *s1, const char *s2,
    size_t n, char **dest)
{
	size_t size1;
	size_t size2;
	char *rval = NULL;

	if (dest == NULL ||
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

/* the one for nvmutil state is in state.c */
/* this one just exits */
void
err_no_cleanup(int stfu, int nvm_errval, const char *msg, ...)
{
	va_list args;
	int saved_errno = errno;
	const char *p;

#if defined(__OpenBSD__) && defined(OpenBSD)
#if (OpenBSD) >= 509
	if (pledge("stdio", NULL) == -1)
		fprintf(stderr, "pledge failure during exit");
#endif
#endif
	if (!errno)
		saved_errno = errno = ECANCELED;

	if ((p = getnvmprogname()) != NULL)
		fprintf(stderr, "%s: ", p);

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	if (p != NULL)
		fprintf(stderr, ": %s\n", strerror(errno));
	else
		fprintf(stderr, "%s\n", strerror(errno));

	exit(EXIT_FAILURE);
}

const char *
getnvmprogname(void)
{
	static char *rval = NULL;
	static char *p;	
	static int setname = 0;

	if (!setname) {
		if ((rval = lbgetprogname(NULL)) == NULL)
			return NULL;

		p = strrchr(rval, '/');
		if (p)
			rval = p + 1;

		setname = 1;
	}

	return rval;
}

/* singleton. if string not null,
   sets the string. after set,
   will not set anymore. either
   way, returns the string
 */
char *
lbgetprogname(char *argv0)
{
	static int setname = 0;
	static char *progname = NULL;
	size_t len;

	if (!setname) {
		if (if_err(argv0 == NULL || *argv0 == '\0', EFAULT) ||
		    slen(argv0, 4096, &len) < 0)
			return NULL;

		memcpy(smalloc(&progname, len + 1), argv0, len + 1);
		setname = 1;
	}

	return progname;
}

int
xpledgex(const char *promises, const char *execpromises)
{
	int saved_errno = errno;
	(void) promises, (void) execpromises, (void) saved_errno;
#ifdef __OpenBSD__
	if (pledge(promises, execpromises) == -1)
		err_no_cleanup(0, errno, "pledge");
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
		err_no_cleanup(0, errno, "pledge");
#endif
	errno = saved_errno;
	return 0;
}
