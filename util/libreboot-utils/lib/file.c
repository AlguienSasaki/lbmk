/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Pathless i/o, and some stuff you
 * probably never saw in userspace.
 *
 * Be nice to the demon.
 */

/*
TODO: putting it here just so it's somewhere:
PATH_MAX is not reliable as a limit for paths,
because the real length depends on mount point,
and specific file systems.
more correct usage example:
long max = pathconf("/", _PC_PATH_MAX);
 */

/* for openat2: */
#ifdef __linux__
#if !defined(USE_OPENAT) || \
    ((USE_OPENAT) < 1) /* if 1: use openat, not openat2 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/common.h"

/* check that a file changed
 */

int
same_file(int fd, struct stat *st_old,
    int check_size)
{
	struct stat st;
	int saved_errno = errno;
	int rval = 0;
	errno = 0;

	if (if_err(st_old == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    (rval = fstat(fd, &st)) < 0 ||
	    (rval = fd_verify_regular(fd, st_old, &st)) < 0 ||
	    if_err(check_size && st.st_size != st_old->st_size, ESTALE))
		return with_fallback_errno(ESTALE);

	reset_caller_errno(rval);
	return 0;
}

int
fsync_dir(const char *path)
{
	int saved_errno = errno;
	size_t pathlen = 0;
	char *dirbuf = NULL;
	int dirfd = -1;
	char *slash = NULL;
	struct stat st = {0};
	int rval = 0;
	errno = 0;

	if (if_err(slen(path, PATH_MAX, &pathlen) == 0, EINVAL))
		goto err_fsync_dir;

	memcpy(smalloc(&dirbuf, pathlen + 1),
	    path, pathlen + 1);
	slash = strrchr(dirbuf, '/');

	if (slash != NULL) {
		*slash = '\0';
		if (*dirbuf == '\0') {
			dirbuf[0] = '/';
			dirbuf[1] = '\0';
		}
	} else {
		dirbuf[0] = '.';
		dirbuf[1] = '\0';
	}

	dirfd = fs_open(dirbuf,
	    O_RDONLY | O_CLOEXEC | O_NOCTTY
#ifdef O_DIRECTORY
	    | O_DIRECTORY
#endif
#ifdef O_NOFOLLOW
	    | O_NOFOLLOW
#endif
);

	if (if_err_sys(dirfd < 0) ||
	    if_err_sys((rval = fstat(dirfd, &st)) < 0) ||
	    if_err(!S_ISDIR(st.st_mode), ENOTDIR)
	    ||
	    if_err_sys((rval = fsync_on_eintr(dirfd)) == -1))
		goto err_fsync_dir;

	xclose(&dirfd);
	free_and_set_null(&dirbuf);

	reset_caller_errno(rval);
	return 0;

err_fsync_dir:
	free_and_set_null(&dirbuf);
	xclose(&dirfd);

	return with_fallback_errno(EIO);
}

/* rw_exact() - Read perfectly or die
 *
 * Read/write, and absolutely insist on an
 * absolute read; e.g. if 100 bytes are
 * requested, this MUST return 100.
 *
 * This function will never return zero.
 * It will only return below (error),
 * or above (success). On error, -1 is
 * returned and errno is set accordingly.
 *
 * Zero-byte returns are not allowed.
 * It will re-spin a finite number of
 * times upon zero-return, to recover,
 * otherwise it will return an error.
 */

ssize_t
rw_exact(int fd, unsigned char *mem, size_t nrw,
    off_t off, int rw_type)
{
	int saved_errno = errno;
	ssize_t rval = 0;
	ssize_t rc = 0;
	size_t nrw_cur;
	off_t off_cur;
	void *mem_cur;
	errno = 0;

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		goto err_rw_exact;

	while (1) {
	
		/* Prevent theoretical overflow */
		if (if_err(rval >= 0 && (size_t)rval > (nrw - (size_t)rc),
		    EOVERFLOW))
			goto err_rw_exact;

		rc += rval;
		if ((size_t)rc >= nrw)
			break;

		mem_cur = (void *)(mem + (size_t)rc);
		nrw_cur = (size_t)(nrw - (size_t)rc);

		if (if_err(off < 0, EOVERFLOW))
			goto err_rw_exact;

		off_cur = off + (off_t)rc;

		if ((rval = rw(fd, mem_cur, nrw_cur, off_cur, rw_type)) <= 0)
			goto err_rw_exact;
	}

	if (if_err((size_t)rc != nrw, EIO) ||
	    (rval = rw_over_nrw(rc, nrw)) < 0)
		goto err_rw_exact;

	reset_caller_errno(rval);
	return rval;

err_rw_exact:
	return with_fallback_errno(EIO);
}

/**
 * rw() - read-write but with more
 * safety checks than barebones libc
 *
 * A fallback is provided for regular read/write.
 * rw_type can be IO_READ (read), IO_WRITE (write),
 * IO_PREAD (pread) or IO_PWRITE
 *
 * WARNING: this function allows zero-byte returns.
 * this is intentional, to mimic libc behaviour.
 * use rw_exact if you need to avoid this.
 * (ditto partial writes/reads)
 *
 */
ssize_t
rw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type)
{
	ssize_t rval = 0;
	ssize_t r = -1;
	int saved_errno = errno;
	errno = 0;

	if (io_args(fd, mem, nrw, off, rw_type) == -1 ||
	    if_err(mem == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(off < 0, EFAULT) ||
	    if_err(nrw == 0, EINVAL))
		return with_fallback_errno(EIO);

	do {
		switch (rw_type) {
		case IO_READ:
			r = read(fd, mem, nrw);
			break;
		case IO_WRITE:
			r = write(fd, mem, nrw);
			break;
		case IO_PREAD:
			r = pread(fd, mem, nrw, off);
			break;
		case IO_PWRITE:
			r = pwrite(fd, mem, nrw, off);
			break;
		default:
			errno = EINVAL;
			break;
		}

	} while (rw_retry(saved_errno, r));
	
	if ((rval = rw_over_nrw(r, nrw)) < 0)
		return with_fallback_errno(EIO);

	reset_caller_errno(rval);
	return rval;
}

int
io_args(int fd, void *mem, size_t nrw,
    off_t off, int rw_type)
{
	int saved_errno = errno;
	errno = 0;

	if (if_err(mem == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(off < 0, ERANGE) ||
	    if_err(!nrw, EPERM) || /* TODO: toggle zero-byte check */
	    if_err(nrw > (size_t)SSIZE_MAX, ERANGE) ||
	    if_err(((size_t)off + nrw) < (size_t)off, ERANGE) ||
	    if_err(rw_type > IO_PWRITE, EINVAL))
		goto err_io_args;

	reset_caller_errno(0);
	return 0;

err_io_args:
	return with_fallback_errno(EINVAL);
}

int
check_file(int fd, struct stat *st)
{
	int saved_errno = errno;
	int rval = 0;
	errno = 0;

	if (if_err(fd < 0, EBADF) ||
	    if_err(st == NULL, EFAULT) ||
	    ((rval = fstat(fd, st)) == -1) ||
	    if_err(!S_ISREG(st->st_mode), EBADF))
		goto err_is_file;

	reset_caller_errno(rval);
	return 0;

err_is_file:
	return with_fallback_errno(EINVAL);
}

/* POSIX can say whatever it wants.
 * specification != implementation
 */
ssize_t
rw_over_nrw(ssize_t r, size_t nrw)
{
	if (if_err(!nrw, EIO) ||
	    (r == -1) ||
	    if_err((size_t)r > SSIZE_MAX, ERANGE) ||
	    if_err((size_t)r > nrw, ERANGE))
		return with_fallback_errno(EIO);

	return r;
}

/* two functions that reduce sloccount by
 * two hundred lines */
int
if_err(int condition, int errval)
{
	if (!condition)
		return 0;
	if (errval)
		errno = errval;
	return 1;
}
int
if_err_sys(int condition)
{
	if (!condition)
		return 0;
	return 1;
}

int
fs_rename_at(int olddirfd, const char *old,
    int newdirfd, const char *new)
{
	if (if_err(new == NULL || old == NULL, EFAULT) ||
	    if_err(olddirfd < 0 || newdirfd < 0, EBADF))
		return -1;

	return renameat(olddirfd, old, newdirfd, new);
}

/* secure open, based on relative path to root
 *
 * always a fixed fd for / see: rootfs()
 * and fs_resolve_at()
 */
int
fs_open(const char *path, int flags)
{
	struct filesystem *fs;

	if (if_err(path == NULL, EFAULT) ||
	    if_err(path[0] != '/', EINVAL) ||
	    if_err_sys((fs = rootfs()) == NULL))
		return -1;

	return fs_resolve_at(fs->rootfd, path + 1, flags);
}

/* singleton function that returns a fixed descriptor of /
 * used throughout, for repeated integrity checks
 */
struct filesystem *
rootfs(void)
{
	static struct filesystem global_fs;
	static int fs_initialised = 0;

	if (!fs_initialised) {

		global_fs.rootfd = -1;

		open_file_on_eintr("/", &global_fs.rootfd,
		    O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0400, NULL);

		if (global_fs.rootfd < 0)
			return NULL;

		fs_initialised = 1;
	}

	return &global_fs;
}

/* filesystem sandboxing in userspace
 * TODO:
	missing length bound check.
	potential CPU DoS on very long paths, spammed repeatedly.
	perhaps cap at MAX_PATH?
 */
int
fs_resolve_at(int dirfd, const char *path, int flags)
{
	int nextfd = -1;
	int curfd;
	const char *p;
	char name[PATH_MAX];
	int saved_errno = errno;
	int r;
	int is_last;
	errno = 0;

	if (dirfd < 0 || path == NULL || *path == '\0') {
		errno = EINVAL;
		return -1;
	}

	p = path;
	curfd = dirfd; /* start here */

	for (;;) {
		r = fs_next_component(&p, name, sizeof(name));
		if (r < 0)
			goto err;
		if (r == 0)
			break;

		is_last = (*p == '\0');

		nextfd = fs_open_component(curfd, name, flags, is_last);
		if (nextfd < 0)
			goto err;

		/* close previous fd if not the original input */
		if (curfd != dirfd)
			xclose(&curfd);

		curfd = nextfd;
		nextfd = -1;
	}

	reset_caller_errno(0);
	return curfd;

err:
	saved_errno = errno;

	if (nextfd >= 0)
		xclose(&nextfd);

	/* close curfd only if it's not the original */
	if (curfd != dirfd && curfd >= 0)
		xclose(&curfd);

	errno = saved_errno;
	return with_fallback_errno(EIO);
}

/* NOTE:
	rejects . and .. but not empty strings
	after normalisation. edge case:
	//////

	normalised implicitly, but might be good
	to add a defensive check regardless. code
	probably not exploitable in current state.
 */
int
fs_next_component(const char **p,
    char *name, size_t namesz)
{
	const char *s = *p;
	size_t len = 0;

	while (*s == '/')
		s++;

	if (*s == '\0') {
		*p = s;
		return 0;
	}

	while (s[len] != '/' && s[len] != '\0')
		len++;

	if (len == 0 || len >= namesz ||
	    len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}

	memcpy(name, s, len);
	name[len] = '\0';

	/* reject . and .. */
	if (if_err((name[0] == '.' && name[1] == '\0') ||
	    (name[0] == '.' && name[1] == '.' && name[2] == '\0'), EPERM))
		goto err;

	*p = s + len;
	return 1;
err:
	return with_fallback_errno(EPERM);
}

int
fs_open_component(int dirfd, const char *name,
    int flags, int is_last)
{
	int saved_errno = errno;
	int fd;
	struct stat st;
	errno = 0;

	fd = openat_on_eintr(dirfd, name,
	    (is_last ? flags : (O_RDONLY | O_DIRECTORY)) |
	    O_NOFOLLOW | O_CLOEXEC, (flags & O_CREAT) ? 0600 : 0);

	if (!is_last &&
	    (if_err(fd < 0, EBADF) ||
	     if_err_sys(fstat(fd, &st) < 0) ||
	     if_err(!S_ISDIR(st.st_mode), ENOTDIR)))
		return with_fallback_errno(EIO);

	reset_caller_errno(fd);
	return fd;
}

int
fs_dirname_basename(const char *path,
    char **dir, char **base,
    int allow_relative)
{
	int saved_errno = errno;
	char *buf = NULL;
	char *slash;
	size_t len;
	errno = 0;

	if (if_err(path == NULL || dir == NULL || base == NULL, EFAULT))
		goto err;

	slen(path, PATH_MAX, &len);
	memcpy(smalloc(&buf, len + 1),
	    path, len + 1);

	/* strip trailing slashes */
	while (len > 1 && buf[len - 1] == '/')
		buf[--len] = '\0';

	slash = strrchr(buf, '/');

	if (slash) {

		*slash = '\0';
		*dir = buf;
		*base = slash + 1;

		if (**dir == '\0') {
			(*dir)[0] = '/';
			(*dir)[1] = '\0';
		}
	} else if (allow_relative) {

		sdup(".", PATH_MAX, dir);
		*base = buf;
	} else {
		free_and_set_null(&buf);
		goto err;
	}

	reset_caller_errno(0);
	return 0;
err:
	return with_fallback_errno(EINVAL);
}

/* TODO: why does this abort, but others
   e.g. open_file_on_eintr, don't???
 */
void
open_file_on_eintr(const char *path,
    int *fd, int flags, mode_t mode,
    struct stat *st)
{
	int saved_errno = errno;
	int rval = 0;
	errno = 0;

	if (path == NULL)
		exitf("open_file_on_eintr: null path");
	if (fd == NULL)
		exitf("%s: open_file_on_eintr: null fd ptr", path);
	if (*fd >= 0)
		exitf(
		    "%s: open_file_on_eintr: file already open", path);

	errno = 0;
	while (fs_retry(saved_errno,
	    rval = open(path, flags, mode)));

	if (rval < 0)
		exitf(
		    "%s: open_file_on_eintr: could not close", path);

	reset_caller_errno(rval);
	*fd = rval;

	/* we don't care about edge case behaviour here,
	   even if the next operation sets errno on success,
	   because the open() call is our main concern.
	   however, we also must preserve the new errno,
	   assuming it changed above under the same edge case */

	saved_errno = errno;

	if (st != NULL) {
		if (fstat(*fd, st) < 0)
			exitf("%s: stat", path);

		if (!S_ISREG(st->st_mode))
			exitf("%s: not a regular file", path);
	}

	if (lseek(*fd, 0, SEEK_CUR) == (off_t)-1)
		exitf("%s: file not seekable", path);

	errno = saved_errno; /* see previous comment */
}


#if defined(__linux__) && \
    (!defined(USE_OPENAT) || ((USE_OPENAT) < 1)) /* we use openat2 on linux */
int
openat_on_eintr(int dirfd, const char *path,
    int flags, mode_t mode)
{
	struct open_how how = {
		.flags = (unsigned long long)flags,
		.mode = mode,
		.resolve =
		    RESOLVE_BENEATH |
		    RESOLVE_NO_SYMLINKS |
		    RESOLVE_NO_MAGICLINKS
	};
	int saved_errno = errno;
	long rval = 0;
	errno = 0;

	if (if_err(dirfd < 0, EBADF) ||
	    if_err(path == NULL, EFAULT))
		goto err;

	errno = 0;
	while (sys_retry(saved_errno,
	    rval = syscall(SYS_openat2, dirfd, path, &how, sizeof(how))));

	if (rval == -1) /* avoid long->int UB for -1 */
		goto err;

	reset_caller_errno(rval);
	return (int)rval;
err:
	return with_fallback_errno(EIO); /* -1 */
}
#else /* regular openat on non-linux e.g. openbsd */
int
openat_on_eintr(int dirfd, const char *path,
    int flags, mode_t mode)
{
	int saved_errno = errno;
	int rval = 0;
	errno = 0;

	if (if_err(dirfd < 0, EBADF) ||
	    if_err(path == NULL, EFAULT))
		return with_fallback_errno(EIO);

	while (fs_retry(saved_errno,
	    rval = openat(dirfd, path, flags, mode)));

	reset_caller_errno(rval);
	return rval;
}
#endif

int
mkdirat_on_eintr(int dirfd, 
    const char *path, mode_t mode)
{
	int saved_errno = errno;
	int rval = 0;
	errno = 0;

	if (if_err(dirfd < 0, EBADF) ||
	    if_err(path == NULL, EFAULT))
		return with_fallback_errno(EIO);

	while (fs_retry(saved_errno,
	    rval = mkdirat(dirfd, path, mode)));

	reset_caller_errno(rval);
	return rval;
}

int
fsync_on_eintr(int fd)
{
	int saved_errno = errno;
	int rval = 0;
	errno = 0;

	if (if_err(fd < 0, EBADF))
		return with_fallback_errno(EIO);

	while (fs_retry(saved_errno,
	    rval = fsync(fd)));

	reset_caller_errno(rval);
	return rval;
}

void
xclose(int *fd)
{
	int saved_errno = errno;
	int rval = 0;

	if (fd == NULL)
		exitf("xclose: null pointer");
	if (*fd < 0)
		return;

	/* nuance regarding EINTR on close():
	 * EINTR can be set on error, but there's
	 * no guarantee whether the fd is then still
	 * open or closed. on some other commands, we
	 * loop EINTR, but for close, we instead skip
	 * aborting *if the errno is EINTR* - so don't
	 * loop it, but do regard EINTR with rval -1
	 * as essenitally a successful close()
	 */

	/* because we don't want to mess with someone
	 * elses file if that fd is then reassigned.
	 * if the operation truly did fail, we ignore
	 * it. just leave it flying in the wind */

	errno = 0;
	if ((rval = close(*fd)) < 0) {
		if (errno != EINTR)
			exitf("xclose: could not close");

		/* regard EINTR as a successful close */
		rval = 0;
	}

	*fd = -1;

	reset_caller_errno(rval);
}

/* unified eintr looping.
 * differently typed functions
 * to avoid potential UB
 *
 * ONE MACRO TO RULE THEM ALL:
 */
#define fs_err_retry() \
	do { \
		if ((rval == -1) && \
		    (errno == EINTR)) \
			return 1; \
		if (rval >= 0 && !errno) \
			errno = saved_errno; \
		return 0; \
	} while(0)
/*
 * Regarding the errno logic above:
 * on success, it is permitted that
 * a syscall could still set errno.
 * We reset errno after storingit
 * for later preservation, in functions
 * that call *_retry() functions.
 *
 * They rely ultimately on this
 * macro for errno restoration. We
 * assume therefore that errno was
 * reset to zero before the retry
 * loop. If errno is then *set* on
 * success, we leave it alone. Otherwise,
 * we restore the caller's saved errno.
 *
 * This offers some consistency, while
 * complying with POSIX specification.
 */


/* retry switch for functions that
   return long status e.g. linux syscall
 */
int
sys_retry(int saved_errno, long rval)
{
	fs_err_retry();
}

/* retry switch for functions that
   return int status e.g. mkdirat
 */
int
fs_retry(int saved_errno, int rval)
{
	fs_err_retry();
}

/* retry switch for functions that
   return rw count in ssize_t e.g. read()
 */
int
rw_retry(int saved_errno, ssize_t rval)
{
	fs_err_retry();
}
