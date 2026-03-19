/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Safe file handling.
 */

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

	if (st_old == NULL || fd < 0)
		goto err_same_file;

	if (fstat(fd, &st) == -1)
		return -1;

	if (st.st_dev != st_old->st_dev ||
	    st.st_ino != st_old->st_ino ||
	    !S_ISREG(st.st_mode))
		goto err_same_file;

	if (check_size &&
	    st.st_size != st_old->st_size)
		goto err_same_file;

	errno = saved_errno;
	return 0;

err_same_file:

	errno = EIO;
	return -1;
}

void
xopen(int *fd_ptr, const char *path, int flags, struct stat *st)
{
	if ((*fd_ptr = open(path, flags)) == -1)
		err(errno, "%s", path);

	if (fstat(*fd_ptr, st) == -1)
		err(errno, "%s: stat", path);

	if (!S_ISREG(st->st_mode))
		err(errno, "%s: not a regular file", path);

	if (lseek(*fd_ptr, 0, SEEK_CUR) == (off_t)-1)
		err(errno, "%s: file not seekable", path);
}

/* Ensure rename() is durable by syncing the
 * directory containing the target file.
 */

int
fsync_dir(const char *path)
{
	int saved_errno = errno;

	unsigned long pathlen;
	unsigned long maxlen;

	char *dirbuf;
	int dirfd;

	char *slash;

	struct stat st;

#if defined(PATH_LEN) && \
    (PATH_LEN) >= 256
	maxlen = PATH_LEN;
#else
	maxlen = 1024;
#endif

	dirbuf = NULL;
	dirfd = -1;

	pathlen = xstrxlen(path, maxlen);

	if (pathlen >= maxlen) {
		fprintf(stderr, "Path too long for fsync_parent_dir\n");
		goto err_fsync_dir;
	}

	if (pathlen == 0)
	{
		errno = EINVAL;
		goto err_fsync_dir;
	}

	dirbuf = malloc(pathlen + 1);
	if (dirbuf == NULL)
		goto err_fsync_dir;

	memcpy(dirbuf, path, pathlen + 1);
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

	dirfd = open(dirbuf, O_RDONLY
#ifdef O_DIRECTORY
	    | O_DIRECTORY
#endif
#ifdef O_NOFOLLOW
	    | O_NOFOLLOW
#endif
	);
	if (dirfd == -1)
		goto err_fsync_dir;

	if (fstat(dirfd, &st) < 0)
		goto err_fsync_dir;

	if (!S_ISDIR(st.st_mode)) {
		fprintf(stderr, "%s: not a directory\n", dirbuf);
		goto err_fsync_dir;
	}

	/* sync file on disk */
	if (fsync_on_eintr(dirfd) == -1)
		goto err_fsync_dir;

	if (close_on_eintr(dirfd) == -1)
		goto err_fsync_dir;

	if (dirbuf != NULL)
		free(dirbuf);

	errno = saved_errno;
	return 0;

err_fsync_dir:
	if (!errno)
		errno = EIO;

	if (errno != saved_errno)
		fprintf(stderr, "%s: %s\n", path, strerror(errno));

	if (dirbuf != NULL)
		free(dirbuf);

	if (dirfd > -1)
		close_on_eintr(dirfd);

	errno = saved_errno;

	return -1;
}

/* create new tmpfile path
 *
 * ON SUCCESS:
 *
 * returns ptr to path string on success
 * ALSO: the int at *fd will be set,
 *       indicating the file descriptor
 *
 * ON ERROR:
 *
 * return NULL (*fd not touched)
 *
 * malloc() may set errno, but you should
 * not rely on errno from this function
 *
 * local: if non-zero, then only a file
 * name will be given, relative to
 * the current file name. for this,
 * the 3rd argument (path) must be non-null
 *
 * if local is zero, then 3rd arg (path)
 * is irrelevant and can be NULL
 */

char *
new_tmpfile(int *fd, int local, const char *path)
{
	unsigned long maxlen;
	struct stat st;

	/* please do not modify the
	 * strings or I will get mad
	 */
	char tmp_none[] = "";
	char tmp_default[] = "/tmp";
	char default_tmpname[] = "tmpXXXXXX";
	char *tmpname;

	char *base = NULL;
	char *dest = NULL;

	unsigned long tmpdir_len = 0;
	unsigned long tmpname_len = 0;
	unsigned long tmppath_len = 0;

	int fd_tmp = -1;
	int flags;

	/*
	 * 256 is the most
	 * conservative path
	 * size limit (posix),
	 * but 4096 is modern
	 *
	 * set PATH_LEN as you
	 * wish, at build time
	 */

#if defined(PATH_LEN) && \
    (PATH_LEN) >= 256
	maxlen = PATH_LEN;
#else
	maxlen = 1024;
#endif

	tmpname = default_tmpname;
	if (local) {
		if (path == NULL)
			goto err_new_tmpfile;
		if (*path == '\0')
			goto err_new_tmpfile;

		if (stat(path, &st) == -1)
			goto err_new_tmpfile;

		if (!S_ISREG(st.st_mode))
			goto err_new_tmpfile;

		tmpname = (char *)path;
	}

	if (local) {
		base = tmp_none;

		/* appended to filename for tmp:
		 */
		tmpdir_len = xstrxlen(default_tmpname, maxlen);
	} else {
		base = x_c_tmpdir();

		if (base == NULL)
			base = tmp_default;
		if (*base == '\0')
			base = tmp_default;

		tmpdir_len = xstrxlen(base, maxlen);
	}

	tmpname_len = xstrxlen(tmpname, maxlen);

	tmppath_len = tmpdir_len + tmpname_len;
	++tmppath_len; /* for '/' or '.' */

	/* max length -1 of maxlen
	 * for termination
	 */
	if (tmpdir_len > maxlen - tmpname_len - 1)
		goto err_new_tmpfile;

	/* +1 for NULL */
	dest = malloc(tmppath_len + 1);
	if (dest == NULL)
		goto err_new_tmpfile;

	if (local) {

		*dest = '.'; /* hidden file */

		memcpy(dest + (unsigned long)1, tmpname, tmpname_len);

		memcpy(dest + (unsigned long)1 + tmpname_len,
		    default_tmpname, tmpdir_len);
	} else {

		memcpy(dest, base, tmpdir_len);

		dest[tmpdir_len] = '/';

		memcpy(dest + tmpdir_len + 1, tmpname, tmpname_len);
	}

	dest[tmppath_len] = '\0';

	fd_tmp = x_i_mkstemp(dest);
	if (fd_tmp == -1)
		goto err_new_tmpfile;

	if (fchmod(fd_tmp, 0600) == -1)
		goto err_new_tmpfile;

	flags = fcntl(fd_tmp, F_GETFL);

	if (flags == -1)
		goto err_new_tmpfile;

	/*
	 * O_APPEND would permit offsets
	 * to be ignored, which breaks
	 * positional read/write
	 */
	if (flags & O_APPEND)
		goto err_new_tmpfile;

	if (lock_file(fd_tmp, flags) == -1)
		goto err_new_tmpfile;

	if (fstat(fd_tmp, &st) == -1)
		goto err_new_tmpfile;

	/*
	 * Extremely defensive
	 * likely pointless checks
	 */

	/* check if it's a file */
	if (!S_ISREG(st.st_mode))
		goto err_new_tmpfile;

	/* check if it's seekable */
	if (lseek(fd_tmp, 0, SEEK_CUR) == (off_t)-1)
		goto err_new_tmpfile;

	/* tmpfile has >1 hardlinks */
	if (st.st_nlink > 1)
		goto err_new_tmpfile;

	/* tmpfile unlinked while opened */
	if (st.st_nlink == 0)
		goto err_new_tmpfile;

	*fd = fd_tmp;

	return dest;

err_new_tmpfile:

	if (dest != NULL)
		free(dest);

	if (fd_tmp > -1)
		close_on_eintr(fd_tmp);

	return NULL;
}

int
lock_file(int fd, int flags)
{
	struct flock fl;

	memset(&fl, 0, sizeof(fl));

	if ((flags & O_ACCMODE) == O_RDONLY)
		fl.l_type = F_RDLCK;
	else
		fl.l_type = F_WRLCK;

	fl.l_whence = SEEK_SET;

	if (fcntl(fd, F_SETLK, &fl) == -1)
		return -1;

	return 0;
}

char *
x_c_tmpdir(void)
{
	char *t;
	struct stat st;

	t = getenv("TMPDIR");

	if (t && *t) {
		if (stat(t, &st) == 0 && S_ISDIR(st.st_mode)) {
			if ((st.st_mode & S_IWOTH) && !(st.st_mode & S_ISVTX))
				return NULL;
			return t;
		}
	}

	if (stat("/tmp", &st) == 0 && S_ISDIR(st.st_mode))
		return "/tmp";

	if (stat("/var/tmp", &st) == 0 && S_ISDIR(st.st_mode))
		return "/var/tmp";

	return ".";
}

/* portable mkstemp
 */

int
x_i_mkstemp(char *template)
{
	int fd;
	int i, j;
	unsigned long len;
	char *p;

	char ch[] =
	    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	unsigned long r;

	len = xstrxlen(template, PATH_LEN);

	/* find trailing XXXXXX */
	if (len < 6)
		return -1;

	p = template + len - 6;

	for (i = 0; i < 100; i++) {

		for (j = 0; j < 6; j++) {

			r = rlong();

			p[j] = ch[(unsigned long)(r >> 1) % (sizeof(ch) - 1)];
		}

		fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600);

		if (fd >= 0)
			return fd;

		if (errno != EEXIST)
			return -1;
	}

	errno = EEXIST;
	return -1;
}

/*
 * Safe I/O functions wrapping around
 * read(), write() and providing a portable
 * analog of both pread() and pwrite().
 * These functions are designed for maximum
 * robustness, checking NULL inputs, overflowed
 * outputs, and all kinds of errors that the
 * standard libc functions don't.
 *
 * Looping on EINTR and EAGAIN is supported.
 * EINTR/EAGAIN looping is done indefinitely.
 */

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

long
rw_file_exact(int fd, unsigned char *mem, unsigned long nrw,
    off_t off, int rw_type, int loop_eagain,
    int loop_eintr, unsigned long max_retries,
    int off_reset)
{
	long rval;
	long rc;

	unsigned long nrw_cur;

	off_t off_cur;
	void *mem_cur;

	unsigned long retries_on_zero;

	rval = 0;

	rc = 0;
	retries_on_zero = 0;

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		return -1;

	while (1) {
	
		/* Prevent theoretical overflow */
		if (rval >= 0 && (unsigned long)rval > (nrw - rc))
			goto err_rw_file_exact;

		rc += rval;
		if ((unsigned long)rc >= nrw)
			break;

		mem_cur = (void *)(mem + (unsigned long)rc);
		nrw_cur = (unsigned long)(nrw - (unsigned long)rc);
		if (off < 0)
			goto err_rw_file_exact;
		off_cur = off + (off_t)rc;

		rval = prw(fd, mem_cur, nrw_cur, off_cur,
		    rw_type, loop_eagain, loop_eintr,
		    off_reset);

		if (rval < 0)
			return -1;

		if (rval == 0) {
			if (retries_on_zero++ < max_retries)
				continue;
			goto err_rw_file_exact;
		}

		retries_on_zero = 0;
	}

	if ((unsigned long)rc != nrw)
		goto err_rw_file_exact;

	return rw_over_nrw(rc, nrw);

err_rw_file_exact:
	errno = EIO;
	return -1;
}

/* prw() - portable read-write
 *
 * This implements a portable analog of pwrite()
 * and pread() - note that this version is not
 * thread-safe (race conditions are possible on
 * shared file descriptors).
 *
 * This limitation is acceptable, since nvmutil is
 * single-threaded. Portability is the main goal.
 *
 * If you need real pwrite/pread, just compile
 * with flag: HAVE_REAL_PREAD_PWRITE=1
 *
 * A fallback is provided for regular read/write.
 * rw_type can be IO_READ, IO_WRITE, IO_PREAD
 * or IO_PWRITE
 *
 * loop_eagain does a retry loop on EAGAIN if set
 * loop_eintr does a retry loop on EINTR if set
 *
 * Unlike the bare syscalls, prw() does security
 * checks e.g. checks NULL strings, checks bounds,
 * also mitigates a few theoretical libc bugs.
 * It is designed for extremely safe single-threaded
 * I/O on applications that need it.
 *
 * NOTE: If you use loop_eagain (1), you enable wait
 * loop on EAGAIN. Beware if using this on a non-blocking
 * pipe (it could spin indefinitely).
 *
 * off_reset: if zero, and using fallback pwrite/pread
 *            analogs, we check if a file offset changed,
 *            which would indicate another thread changed
 *            it, and return error, without resetting the
 *            file - this would allow that thread to keep
 *            running, but we could then cause a whole
 *            program exit if we wanted to.
 *            if not zero:
 *            we reset and continue, and pray for the worst.
 */

long
prw(int fd, void *mem, unsigned long nrw,
    off_t off, int rw_type,
    int loop_eagain, int loop_eintr,
    int off_reset)
{
	long r;
	int positional_rw;
	struct stat st;
#if !defined(HAVE_REAL_PREAD_PWRITE) || \
    HAVE_REAL_PREAD_PWRITE < 1
	int saved_errno;
	off_t verified;
	off_t off_orig;
	off_t off_last;
#endif

	if (io_args(fd, mem, nrw, off, rw_type)
	    == -1) {
		return -1;
	}

	r = -1;

	/* Programs like cat can use this,
	   so we only check if it's a normal
	   file if not looping EAGAIN */
	if (!loop_eagain) {
		/*
		 * Checking on every run of prw()
		 * is expensive if called many
		 * times, but is defensive in
		 * case the status changes.
		 */
		if (check_file(fd, &st) == -1)
			return -1;
	}

	if (rw_type >= IO_PREAD)
		positional_rw = 1; /* pread/pwrite */
	else
		positional_rw = 0; /* read/write */

try_rw_again:

	if (!positional_rw) {
#if defined(HAVE_REAL_PREAD_PWRITE) && \
    HAVE_REAL_PREAD_PWRITE > 0
real_pread_pwrite:
#endif
		if (rw_type == IO_WRITE)
			r = write(fd, mem, nrw);
		else if (rw_type == IO_READ)
			r = read(fd, mem, nrw);
#if defined(HAVE_REAL_PREAD_PWRITE) && \
    HAVE_REAL_PREAD_PWRITE > 0
		else if (rw_type == IO_PWRITE)
			r = pwrite(fd, mem, nrw, off);
		else if (rw_type == IO_PREAD)
			r = pread(fd, mem, nrw, off);
#endif

		if (r == -1 && (errno == try_err(loop_eintr, EINTR)
		    || errno == try_err(loop_eagain, EAGAIN)))
			goto try_rw_again;

		return rw_over_nrw(r, nrw);
	}

#if defined(HAVE_REAL_PREAD_PWRITE) && \
    HAVE_REAL_PREAD_PWRITE > 0
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

		/*
		 * Partial thread-safety: detect
		 * if the offset changed to what
		 * we previously got. If it did,
		 * then another thread may have
		 * changed it. Enabled if
		 * off_reset is OFF_RESET.
		 *
		 * We do this *once*, on the theory
		 * that nothing is touching it now.
		 */
		if (off_reset && off != verified)
			lseek_on_eintr(fd, off, SEEK_SET,
			    loop_eagain, loop_eintr);

		do {
			/*
			 * Verify again before I/O
			 * (even with OFF_ERR)
			 *
			 * This implements the first check
			 * even with OFF_ERR, but without
			 * the recovery. On ERR_RESET, if
			 * the check fails again, then we
			 * know something else is touching
			 * the file, so it's best that we
			 * probably leave it alone and err.
			 *
			 * In other words, ERR_RESET only
			 * tolerates one change. Any more
			 * will cause an exit, including
			 * per EINTR/EAGAIN re-spin.
			 */
			verified = lseek_on_eintr(fd, (off_t)0, SEEK_CUR,
			    loop_eagain, loop_eintr);

			if (off != verified)
				goto err_prw;

			if (rw_type == IO_PREAD)
				r = read(fd, mem, nrw);
			else if (rw_type == IO_PWRITE)
				r = write(fd, mem, nrw);

			if (rw_over_nrw(r, nrw) == -1) {
				errno = EIO;
				break;
			}

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

	return rw_over_nrw(r, nrw);
#endif

err_prw:
	errno = EIO;
	return -1;
}

int
io_args(int fd, void *mem, unsigned long nrw,
    off_t off, int rw_type)
{
	/* obviously */
	if (mem == NULL)
		goto err_io_args;

	/* uninitialised fd */
	if (fd < 0)
		goto err_io_args;

	/* negative offset */
	if (off < 0)
		goto err_io_args;

	/* prevent zero-byte rw */
	if (!nrw)
		goto err_io_args;

	/* prevent overflow */
	if (nrw > (unsigned long)X_LONG_MAX)
		goto err_io_args;

	/* prevent overflow */
	if (((unsigned long)off + nrw) < (unsigned long)off)
		goto err_io_args;

	if (rw_type > IO_PWRITE)
		goto err_io_args;

	return 0;

err_io_args:
	errno = EIO;
	return -1;
}

int
check_file(int fd, struct stat *st)
{
	if (fstat(fd, st) == -1)	
		goto err_is_file;

	if (!S_ISREG(st->st_mode))
		goto err_is_file;

	return 0;

err_is_file:
	errno = EIO;
	return -1;
}

/* Check weirdness on buggy libc.
 *
 * POSIX can say whatever it wants.
 * specification != implementation
 */

long
rw_over_nrw(long r, unsigned long nrw)
{
	/* not a libc bug, but we
	 * don't like the number zero
	 */
	if (!nrw)
		goto err_rw_over_nrw;

	if (r == -1)
		return r;

	if ((unsigned long)
	    r > X_LONG_MAX) {

		/* Theoretical buggy libc
		 * check. Extremely academic.
		 *
		 * Specifications never
		 * allow this return value
		 * to exceed SSIZE_T, but
		 * spec != implementation
		 *
		 * Check this after using
		 * [p]read() or [p]write()
		 *
		 * NOTE: here, we assume
		 * long integers are the
		 * same size as SSIZE_T
		 */

		goto err_rw_over_nrw;
	}

	/* Theoretical buggy libc:
	 * Should never return a number of
	 * bytes above the requested length.
	 */
	if ((unsigned long)r > nrw)
		goto err_rw_over_nrw;

	return r;

err_rw_over_nrw:

	errno = EIO;
	return -1;
}

#if !defined(HAVE_REAL_PREAD_PWRITE) || \
    HAVE_REAL_PREAD_PWRITE < 1
/*
 * lseek_on_eintr() does lseek() but optionally
 * on an EINTR/EAGAIN wait loop. Used by prw()
 * for setting offsets for positional I/O.
 */
off_t
lseek_on_eintr(int fd, off_t off, int whence,
    int loop_eagain, int loop_eintr)
{
	off_t old;

	old = -1;

	do {
		old = lseek(fd, off, whence);
	} while (old == (off_t)-1 && (
	    errno == try_err(loop_eintr, EINTR) ||
	    errno == try_err(loop_eagain, EAGAIN)));

	return old;
}
#endif

int
try_err(int loop_err, int errval)
{
	if (loop_err)
		return errval;

	return -1;
}

int
close_on_eintr(int fd)
{
	int r;
	int saved_errno = errno;

	do {
		r = close(fd);
	} while (r == -1 && errno == EINTR);

	if (r > -1)
		errno = saved_errno;

	return r;
}

int
fsync_on_eintr(int fd)
{
	int r;

	do {
		r = fsync(fd);
	} while (r == -1 && errno == EINTR);

	return r;
}
