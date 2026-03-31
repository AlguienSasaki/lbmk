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


#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* for openat2: */
#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif

#include "../include/common.h"

/* check that a file changed
 */

int
same_file(int fd, struct stat *st_old,
    int check_size)
{
	struct stat st;
	int saved_errno = errno;

	/* TODO: null/-1 checks
	 *       like this can be
	 *       generalised
	 */
	if (st_old == NULL) {
		errno = EFAULT;
		goto err_same_file;
	}
	if (fd < 0) {
		errno = EBADF;
		goto err_same_file;
	}

	if (fstat(fd, &st) == -1)
		goto err_same_file;

	if (fd_verify_regular(fd, st_old, &st) < 0)
		goto err_same_file;

	if (check_size &&
	    st.st_size != st_old->st_size)
		goto err_same_file;

	errno = saved_errno;
	return 0;

err_same_file:
	return set_errno(saved_errno, ESTALE);
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

	int close_errno;

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
	    if_err_sys(fstat(dirfd, &st) < 0) ||
	    if_err(!S_ISDIR(st.st_mode), ENOTDIR)
	    ||
	    if_err_sys(fsync_on_eintr(dirfd) == -1))
		goto err_fsync_dir;

	close_on_eintr(&dirfd);

	free_and_set_null(&dirbuf);

	errno = saved_errno;
	return 0;

err_fsync_dir:


	free_and_set_null(&dirbuf);
	close_on_eintr(&dirfd);

	return set_errno(saved_errno, EIO);
}

/* rw_file_exact() - Read perfectly or die
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
rw_file_exact(int fd, unsigned char *mem, size_t nrw,
    off_t off, int rw_type, int loop_eagain,
    int loop_eintr, size_t max_retries,
    int off_reset)
{
	ssize_t rval;
	ssize_t rc;

	size_t nrw_cur;

	off_t off_cur;
	void *mem_cur;

	size_t retries_on_zero;

	int saved_errno = errno;
	errno = 0;

	rval = 0;

	rc = 0;
	retries_on_zero = 0;

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		goto err_rw_file_exact;

	while (1) {
	
		/* Prevent theoretical overflow */
		if (rval >= 0 && (size_t)rval > (nrw - rc)) {
			errno = EOVERFLOW;
			goto err_rw_file_exact;
		}

		rc += rval;
		if ((size_t)rc >= nrw)
			break;

		mem_cur = (void *)(mem + (size_t)rc);
		nrw_cur = (size_t)(nrw - (size_t)rc);

		if (off < 0) {
			errno = EOVERFLOW;
			goto err_rw_file_exact;
		}

		off_cur = off + (off_t)rc;

		rval = prw(fd, mem_cur, nrw_cur, off_cur,
		    rw_type, loop_eagain, loop_eintr,
		    off_reset);

		if (rval < 0)
			goto err_rw_file_exact;

		if (rval == 0) {
			if (retries_on_zero++ < max_retries)
				continue;

			errno = EIO;
			goto err_rw_file_exact;
		}

		retries_on_zero = 0;
	}

	if ((size_t)rc != nrw) {

		errno = EIO;
		goto err_rw_file_exact;
	}

	rval = rw_over_nrw(rc, nrw);
	if (rval < 0)
		goto err_rw_file_exact;

	errno = saved_errno;

	return rval;

err_rw_file_exact:

	return set_errno(saved_errno, EIO);
}

/* prw() - portable read-write with more
 * safety checks than barebones libc
 *
 * If you need real pwrite/pread, just compile
 * with flag: REAL_POS_IO=1
 *
 * A fallback is provided for regular read/write.
 * rw_type can be IO_READ (read), IO_WRITE (write),
 * IO_PREAD (pread) or IO_PWRITE
 *
 * loop_eagain does a retry loop on EAGAIN if set
 * loop_eintr does a retry loop on EINTR if set
 *
 * race conditions on non-libc pread/pwrite:
 * if a file offset changes, abort, to mitage.
 *
 * off_reset 1: reset the file offset *once* if
 *              a change was detected, assuming
 *              nothing else is touching it now
 * off_reset 0: never reset if changed
 *
 * REAL_POS_IO is enabled by default in common.h
 * and the fallback version was written for fun.
 * You should just use the real one (REAL_POS_IO 1),
 * since it is generally more reliable.
 */

ssize_t
prw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type,
    int loop_eagain, int loop_eintr,
    int off_reset)
{
	ssize_t rval;
	ssize_t r;
	int positional_rw;
	struct stat st;
#if !defined(REAL_POS_IO) || \
    REAL_POS_IO < 1
	off_t verified;
	off_t off_orig;
	off_t off_last;
#endif
	int saved_errno = errno;
	errno = 0;

	if (io_args(fd, mem, nrw, off, rw_type)
	    == -1)
		goto err_prw;

	r = -1;

	/* do not use loop_eagain on
	 * normal files
	 */

	if (!loop_eagain) {
		/* check whether the file
		 * changed
		 */

		if (check_file(fd, &st) == -1)
			goto err_prw;
	}

	if (rw_type >= IO_PREAD)
		positional_rw = 1; /* pread/pwrite */
	else
		positional_rw = 0; /* read/write */

try_rw_again:

	if (!positional_rw) {
#if defined(REAL_POS_IO) && \
    REAL_POS_IO > 0
real_pread_pwrite:
#endif
		if (rw_type == IO_WRITE)
			r = write_on_eintr(fd, mem, nrw);
		else if (rw_type == IO_READ)
			r = read_on_eintr(fd, mem, nrw);
#if defined(REAL_POS_IO) && \
    REAL_POS_IO > 0
		else if (rw_type == IO_PWRITE)
			r = pwrite_on_eintr(fd, mem, nrw, off);
		else if (rw_type == IO_PREAD)
			r = pread_on_eintr(fd, mem, nrw, off);
#endif

		if (r == -1 && (errno == try_err(loop_eintr, EINTR)
		    || errno == try_err(loop_eagain, EAGAIN)))
			goto try_rw_again;

		rval = rw_over_nrw(r, nrw);
		if (rval < 0)
			goto err_prw;

		errno = saved_errno;

		return rval;
	}

#if defined(REAL_POS_IO) && \
    REAL_POS_IO > 0
	goto real_pread_pwrite;
#else
	if ((off_orig = lseek_on_eintr(fd, (off_t)0, SEEK_CUR,
	    loop_eagain, loop_eintr)) == (off_t)-1) {
		r = -1;
	} else if (lseek_on_eintr(fd, off, SEEK_SET,
	    loop_eagain, loop_eintr) == (off_t)-1) {
		r = -1;
	} else {
		verified = lseek_on_eintr(fd, (off_t)0, SEEK_CUR,
		    loop_eagain, loop_eintr);

		/* abort if the offset changed,
		 * indicating race condition. if
		 * off_reset enabled, reset *ONCE*
		 */

		if (off_reset && off != verified)
			lseek_on_eintr(fd, off, SEEK_SET,
			    loop_eagain, loop_eintr);

		do {
			/* check offset again, repeatedly.
			 * even if off_reset is set, this
			 * aborts if offsets change again
			 */

			verified = lseek_on_eintr(fd, (off_t)0, SEEK_CUR,
			    loop_eagain, loop_eintr);

			if (off != verified) {

				errno = EBUSY;
				goto err_prw;
			}

			if (rw_type == IO_PREAD)
				r = read_on_eintr(fd, mem, nrw);
			else if (rw_type == IO_PWRITE)
				r = write_on_eintr(fd, mem, nrw);

			if (rw_over_nrw(r, nrw) == -1)
				break;

		} while (r == -1 &&
		    (errno == try_err(loop_eintr, EINTR) ||
		     errno == try_err(loop_eagain, EAGAIN)));
	}

	saved_errno = errno;

	off_last = lseek_on_eintr(fd, off_orig, SEEK_SET,
	    loop_eagain, loop_eintr);

	if (off_last != off_orig) {
		errno = saved_errno;
		goto err_prw;
	}

	errno = saved_errno;

	rval = rw_over_nrw(r, nrw);
	if (rval < 0)
		goto err_prw;

	errno = saved_errno;

	return rval;

#endif

err_prw:
	return set_errno(saved_errno, EIO);
}

int
io_args(int fd, void *mem, size_t nrw,
    off_t off, int rw_type)
{
	int saved_errno = errno;

	if (if_err(mem == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(off < 0, ERANGE) ||
	    if_err(!nrw, EPERM) || /* TODO: toggle zero-byte check */
	    if_err(nrw > (size_t)SSIZE_MAX, ERANGE) ||
	    if_err(((size_t)off + nrw) < (size_t)off, ERANGE) ||
	    if_err(rw_type > IO_PWRITE, EINVAL))
		goto err_io_args;

	errno = saved_errno;
	return 0;

err_io_args:
	return set_errno(saved_errno, EINVAL);
}

int
check_file(int fd, struct stat *st)
{
	int saved_errno = errno;

	if (if_err(fd < 0, EBADF) ||
	    if_err(st == NULL, EFAULT) ||
	    if_err(fstat(fd, st) == -1, 0) ||
	    if_err(!S_ISREG(st->st_mode), EBADF))
		goto err_is_file;

	errno = saved_errno;
	return 0;

err_is_file:
	return set_errno(saved_errno, EINVAL);
}

/* POSIX can say whatever it wants.
 * specification != implementation
 */
ssize_t
rw_over_nrw(ssize_t r, size_t nrw)
{
	int saved_errno = errno;

	if (if_err(!nrw, 0) ||
	    if_err(r == -1, 0) ||
	    if_err((size_t)r > SSIZE_MAX, ERANGE) ||
	    if_err((size_t)r > nrw, ERANGE))
		return set_errno(saved_errno, EIO);

	errno = saved_errno;
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
/* errno can never be -1, so you can
 * use this to conditionally set an integer
 * for comparison. see example in lseek_on_eintr
 */
int
try_err(int loop_err, int errval)
{
	if (loop_err)
		return errval;
	return -1;
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
			close_on_eintr(&curfd);

		curfd = nextfd;
		nextfd = -1;
	}

	errno = saved_errno;
	return curfd;

err:
	saved_errno = errno;

	if (nextfd >= 0)
		close_on_eintr(&nextfd);

	/* close curfd only if it's not the original */
	if (curfd != dirfd && curfd >= 0)
		close_on_eintr(&curfd);

	errno = saved_errno;
	return -1;
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
	if ((name[0] == '.' && name[1] == '\0') ||
	    (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
		errno = EPERM;
		return -1;
	}

	*p = s + len;
	return 1;
}

int
fs_open_component(int dirfd, const char *name,
    int flags, int is_last)
{
	int fd;
	struct stat st;

	fd = openat_on_eintr(dirfd, name,
	    (is_last ? flags : (O_RDONLY | O_DIRECTORY)) |
	    O_NOFOLLOW | O_CLOEXEC, (flags & O_CREAT) ? 0600 : 0);

	if (!is_last) {

		if (if_err(fd < 0, EBADF) ||
		    if_err_sys(fstat(fd, &st) < 0))
			return -1;

		if (!S_ISDIR(st.st_mode)) {

			close_on_eintr(&fd);
			errno = ENOTDIR;
			return -1;
		}
	}

	return fd;
}

int
fs_dirname_basename(const char *path,
    char **dir, char **base,
    int allow_relative)
{
	char *buf = NULL;
	char *slash;
	size_t len;
	int rval;

	if (if_err(path == NULL || dir == NULL || base == NULL, EFAULT))
		return -1;

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
		errno = EINVAL;
		free_and_set_null(&buf);
		return -1;
	}

	return 0;
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
	int rval;

	if (path == NULL)
		err_exit(EINVAL, "open_file_on_eintr: null path");
	if (fd == NULL)
		err_exit(EFAULT, "%s: open_file_on_eintr: null fd ptr", path);
	if (*fd >= 0)
		err_exit(EBADF,
		    "%s: open_file_on_eintr: file already open", path);

	errno = 0;
	while (fs_retry(saved_errno,
	    rval = open(path, flags, mode)));

	if (rval < 0)
		err_exit(errno,
		    "%s: open_file_on_eintr: could not close", path);

	*fd = rval;

	if (st != NULL) {
		if (fstat(*fd, st) < 0)
			err_exit(errno, "%s: stat", path);

		if (!S_ISREG(st->st_mode))
			err_exit(errno, "%s: not a regular file", path);
	}

	if (lseek_on_eintr(*fd, 0, SEEK_CUR, 1, 1) == (off_t)-1)
		err_exit(errno, "%s: file not seekable", path);

	errno = saved_errno;
}


#ifdef __linux__ /* we use openat2 on linux */
int
openat_on_eintr(int dirfd, const char *path,
    int flags, mode_t mode)
{
	struct open_how how = {
		.flags = flags,
		.mode = mode,
		.resolve =
		    RESOLVE_BENEATH |
		    RESOLVE_NO_SYMLINKS |
		    RESOLVE_NO_MAGICLINKS
	};
	int saved_errno = errno;
	long rval;

	if (if_err(dirfd < 0, EBADF) ||
	    if_err(path == NULL, EFAULT))
		return set_errno(saved_errno, EIO);

	errno = 0;
	while (sys_retry(saved_errno,
	    rval = syscall(SYS_openat2, dirfd, path, &how, sizeof(how))));

	if (rval == -1) /* avoid long->int UB for -1 */
		return -1;

	return (int)rval;
}
#else /* regular openat on non-linux e.g. openbsd */
int
openat_on_eintr(int dirfd, const char *path,
    int flags, mode_t mode)
{
	int saved_errno = errno;
	int rval;

	if (if_err(dirfd < 0, EBADF) ||
	    if_err(path == NULL, EFAULT))
		return set_errno(saved_errno, EIO);

	errno = 0;
	while (fs_retry(saved_errno,
	    rval = openat(dirfd, path, flags, mode)));

	return rval;
}
#endif

off_t
lseek_on_eintr(int fd, off_t off, int whence,
    int loop_eagain, int loop_eintr)
{
	int saved_errno = errno;
	off_t rval;

	errno = 0;
	while (off_retry(saved_errno,
	    rval = lseek(fd, off, whence)));

	return rval;
}

int
mkdirat_on_eintr(int dirfd, 
    const char *path, mode_t mode)
{
	int saved_errno = errno;
	int rval;

	if (if_err(dirfd < 0, EBADF) ||
	    if_err(path == NULL, EFAULT))
		return set_errno(saved_errno, EIO);

	errno = 0;
	while (fs_retry(saved_errno,
	    rval = mkdirat(dirfd, path, mode)));

	return rval;
}

ssize_t
read_on_eintr(int fd,
    void *buf, size_t count)
{
	int saved_errno = errno;
	ssize_t rval;

	if (if_err(buf == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(count == 0, EINVAL))
		return set_errno(saved_errno, EIO);

	errno = 0;
	while (rw_retry(saved_errno,
	    rval = read(fd, buf, count)));

	return rval;
}

ssize_t
pread_on_eintr(int fd,
    void *buf, size_t count,
    off_t off)
{
	int saved_errno = errno;
	ssize_t rval;

	if (if_err(buf == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(off < 0, EFAULT) ||
	    if_err(count == 0, EINVAL))
		return set_errno(saved_errno, EIO);

	errno = 0;
	while (rw_retry(saved_errno,
	    rval = pread(fd, buf, count, off)));

	return rval;
}

ssize_t
write_on_eintr(int fd,
    void *buf, size_t count)
{
	int saved_errno = errno;
	ssize_t rval;

	if (if_err(buf == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(count == 0, EINVAL))
		return set_errno(saved_errno, EIO);

	errno = 0;
	while (rw_retry(saved_errno,
	    rval = write(fd, buf, count)));

	return rval;
}

ssize_t
pwrite_on_eintr(int fd,
    void *buf, size_t count,
    off_t off)
{
	int saved_errno = errno;
	ssize_t rval;

	if (if_err(buf == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(off < 0, EFAULT) ||
	    if_err(count == 0, EINVAL))
		return set_errno(saved_errno, EIO);

	errno = 0;
	while (rw_retry(saved_errno,
	    rval = pwrite(fd, buf, count, off)));

	return rval;
}

int
fsync_on_eintr(int fd)
{
	int saved_errno = errno;
	int rval;

	if (if_err(fd < 0, EBADF))
		return set_errno(saved_errno, EIO);

	errno = 0;
	while (fs_retry(saved_errno,
	    rval = fsync(fd)));

	return rval;
}

void
close_on_eintr(int *fd)
{
	int saved_errno = errno;
	int rval;

	if (fd == NULL)
		err_exit(EINVAL, "close_on_eintr: null pointer");
	if (*fd < 0)
		return;

	errno = 0;
	while (fs_retry(saved_errno,
	    rval = close(*fd)));

	if (rval < 0)
		err_exit(errno, "close_on_eintr: could not close");

	*fd = -1;
}

/* unified eintr looping.
 * differently typed functions
 * to avoid potential UB
 *
 * ONE MACRO TO RULE THEM ALL:
 */
#define fs_err_retry() \
	if ((rval == -1) && \
	    (errno == EINTR)) \
		return 1; \
	if (rval >= 0 && !errno) \
		errno = saved_errno; \
	return 0
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


/* retry switch for offset-based
 * functions e.g. lseek
 */
/* retry switch for functions that
   return long status e.g. linux syscall
 */
int
off_retry(int saved_errno, off_t rval)
{
	fs_err_retry();
}

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
