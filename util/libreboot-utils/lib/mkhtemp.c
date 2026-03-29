/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Hardened mktemp (be nice to the demon).
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
/* for openat2 syscall on linux */
#define _GNU_SOURCE 1
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* for openat2 / fast path: */
#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#ifndef O_TMPFILE
#define O_TMPFILE 020000000
#endif
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif
#endif

#include "../include/common.h"

/* note: tmpdir is an override of TMPDIR or /tmp or /var/tmp */
int
new_tmpfile(int *fd, char **path, char *tmpdir,
    const char *template)
{
	return new_tmp_common(fd, path, MKHTEMP_FILE,
	    tmpdir, template);
}

/* note: tmpdir is an override of TMPDIR or /tmp or /var/tmp */
int
new_tmpdir(int *fd, char **path, char *tmpdir,
    const char *template)
{
	return new_tmp_common(fd, path, MKHTEMP_DIR,
	    tmpdir, template);
}

int
new_tmp_common(int *fd, char **path, int type,
    char *tmpdir, const char *template)
{
#if defined(PATH_LEN) && \
    (PATH_LEN) >= 256
	size_t maxlen = PATH_LEN;
#else
	size_t maxlen = 4096;
#endif
	struct stat st;

	const char *templatestr;
	size_t templatestr_len;

	size_t dirlen;
	size_t destlen;
	char *dest = NULL; /* final path (will be written into "path") */
	int saved_errno = errno;
	int dirfd = -1;
	const char *fname = NULL;

	struct stat st_dir_initial;

	char *fail_dir = NULL;

	if (path == NULL || fd == NULL) {
		errno = EFAULT;
		goto err;
	}

	/* don't mess with someone elses file */
	if (*fd >= 0) {
		errno = EEXIST;
		goto err;
	}

	/* regarding **path:
	* the pointer (to the pointer)
	* must nott be null, but we don't
	* care about the pointer it points
	* to. you should expect it to be
	* replaced upon successful return
	*
	* (on error, it will not be touched)
	*/

	*fd = -1;

	if (tmpdir == NULL) { /* no user override */
#if defined(PERMIT_NON_STICKY_ALWAYS) && \
    ((PERMIT_NON_STICKY_ALWAYS) > 0)
		tmpdir = env_tmpdir(PERMIT_NON_STICKY_ALWAYS, &fail_dir, NULL);
#else
		tmpdir = env_tmpdir(0, &fail_dir, NULL);
#endif
	} else {

#if defined(PERMIT_NON_STICKY_ALWAYS) && \
    ((PERMIT_NON_STICKY_ALWAYS) > 0)
		tmpdir = env_tmpdir(PERMIT_NON_STICKY_ALWAYS, &fail_dir,
		    tmpdir);
#else
		tmpdir = env_tmpdir(0, &fail_dir, tmpdir);
#endif
	}
	if (tmpdir == NULL)
		goto err;

	if (slen(tmpdir, maxlen, &dirlen) < 0)
		goto err;
	if (*tmpdir == '\0')
		goto err;
	if (*tmpdir != '/')
		goto err;

	if (template != NULL)
		templatestr = template;
	else
		templatestr = "tmp.XXXXXXXXXX";

	if (slen(templatestr, maxlen, &templatestr_len) < 0)
		goto err;

	/* may as well calculate in advance */
	destlen = dirlen + 1 + templatestr_len;
	/* full path: */
	if (scatn(3, (const char *[]) { tmpdir, "/", templatestr },
	    maxlen, &dest) < 0)
		goto err;

	fname = dest + dirlen + 1;

	dirfd = fs_open(tmpdir,
	    O_RDONLY | O_DIRECTORY);
	if (dirfd < 0)
		goto err;

	if (fstat(dirfd, &st_dir_initial) < 0)
		goto err;

	*fd = mkhtemp(fd, &st, dest, dirfd,
	    fname, &st_dir_initial, type);
	if (*fd < 0)
		goto err;

	close_on_eintr(&dirfd);

	errno = saved_errno;
	*path = dest;

	return 0;

err:

	if (errno != saved_errno)
		saved_errno = errno;
	else
		saved_errno = errno = EIO;

	free_and_set_null(&dest);

	close_on_eintr(&dirfd);
	close_on_eintr(fd);

	/* where a TMPDIR isn't found, and we err,
	 * we pass this back through for the
	 * error message
	 */
	if (fail_dir != NULL)
		*path = fail_dir;

	errno = saved_errno;
	return -1;
}


/* hardened TMPDIR parsing
 */

char *
env_tmpdir(int bypass_all_sticky_checks, char **tmpdir,
    char *override_tmpdir)
{
	char *t;
	int allow_noworld_unsticky;
	int saved_errno = errno;

	static const char tmp[] = "/tmp";
	static const char vartmp[] = "/var/tmp";

	/* tmpdir is a user override, if set */
	if (override_tmpdir == NULL)
		t = getenv("TMPDIR");
	else
		t = override_tmpdir;

	if (t != NULL && *t != '\0') {

		if (tmpdir_policy(t,
		    &allow_noworld_unsticky) < 0) {
			if (tmpdir != NULL)
				*tmpdir = t;
			return NULL; /* errno already set */
		}

		if (!world_writeable_and_sticky(t,
		    allow_noworld_unsticky,
		    bypass_all_sticky_checks)) {
			if (tmpdir != NULL)
				*tmpdir = t;
			return NULL;
		}

		errno = saved_errno;
		return t;
	}

	allow_noworld_unsticky = 0;

	if (world_writeable_and_sticky(tmp,
	    allow_noworld_unsticky,
	    bypass_all_sticky_checks)) {

		if (tmpdir != NULL)
			*tmpdir = (char *)tmp;

		errno = saved_errno;
		return (char *)tmp;
	}

	if (world_writeable_and_sticky(vartmp,
	    allow_noworld_unsticky,
	    bypass_all_sticky_checks)) {

		if (tmpdir != NULL)
			*tmpdir = (char *)vartmp;

		errno = saved_errno;
		return (char *)vartmp;
	}

	return NULL;
}

int
tmpdir_policy(const char *path,
    int *allow_noworld_unsticky)
{
	int saved_errno = errno;
	int r;

	if (path == NULL ||
	    allow_noworld_unsticky == NULL) {

		errno = EFAULT;
		return -1;
	}

	*allow_noworld_unsticky = 1;

	r = same_dir(path, "/tmp");
	if (r < 0)
		goto err_tmpdir_policy;
	if (r > 0)
		*allow_noworld_unsticky = 0;

	r = same_dir(path, "/var/tmp");
	if (r < 0)
		goto err_tmpdir_policy;
	if (r > 0)
		*allow_noworld_unsticky = 0;

	errno = saved_errno;
	return 0;

err_tmpdir_policy:
	return set_errno(saved_errno, EIO);
}

int
same_dir(const char *a, const char *b)
{
	int fd_a = -1;
	int fd_b = -1;

	struct stat st_a;
	struct stat st_b;

	int saved_errno = errno;
	int rval_scmp;

#if defined(PATH_LEN) && \
    (PATH_LEN) >= 256
	size_t maxlen = (PATH_LEN);
#else
	size_t maxlen = 4096;
#endif

	/* optimisation: if both dirs
	   are the same, we don't need
	   to check anything. sehr schnell:
	 */
	if (scmp(a, b, maxlen, &rval_scmp) < 0)
		goto err_same_dir;
	/* bonus: scmp checks null for us */
	if (rval_scmp == 0)
		goto success_same_dir;

	fd_a = fs_open(a, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd_a < 0)
		goto err_same_dir;

	fd_b = fs_open(b, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd_b < 0)
		goto err_same_dir;

	if (fstat(fd_a, &st_a) < 0)
		goto err_same_dir;

	if (fstat(fd_b, &st_b) < 0)
		goto err_same_dir;

	if (st_a.st_dev == st_b.st_dev &&
	    st_a.st_ino == st_b.st_ino) {

		close_on_eintr(&fd_a);
		close_on_eintr(&fd_b);

success_same_dir:

		/* SUCCESS
		 */

		errno = saved_errno;
		return 1;
	}

	close_on_eintr(&fd_a);
	close_on_eintr(&fd_b);

	/* FAILURE (logical)
	 */

	errno = saved_errno;
	return 0;

err_same_dir:

	/* FAILURE (probably syscall)
	 */

	close_on_eintr(&fd_a);
	close_on_eintr(&fd_b);

	return set_errno(saved_errno, EIO);
}

/* bypass_all_sticky_checks: if set,
       disable stickiness checks (libc behaviour)
       (if not set: leah behaviour)

   allow_noworld_unsticky:
       allow non-sticky files if not world-writeable
       (still block non-sticky in standard TMPDIR)
*/
int
world_writeable_and_sticky(
    const char *s,
    int allow_noworld_unsticky,
    int bypass_all_sticky_checks)
{
	struct stat st;
	int dirfd = -1;

	int saved_errno = errno;

	if (s == NULL || *s == '\0') {
		errno = EINVAL;
		goto sticky_hell;
	}

	/* mitigate symlink attacks*
	 */
	dirfd = fs_open(s, O_RDONLY | O_DIRECTORY);
	if (dirfd < 0)
		goto sticky_hell;

	if (fstat(dirfd, &st) < 0)
		goto sticky_hell;

	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		goto sticky_hell;
	}

	/* all of these checks are probably
	 * redundant (execution rights)
	if (!(st.st_mode & S_IXUSR) ||
	    !(st.st_mode & S_IXGRP) ||
	    !(st.st_mode & S_IXOTH)) {
	*/
	/* just require it for *you*, for now */
	if (!(st.st_mode & S_IXUSR)) {
		errno = EACCES;
		goto sticky_hell;
	}

	/* *normal-**ish mode (libc):
	 */

	if (bypass_all_sticky_checks)
		goto sticky_heaven; /* normal == no security */

	/* extremely not-libc mode:
	 */

	if (st.st_mode & S_IWOTH) { /* world writeable */

		/* if world-writeable, only
		 * allow sticky files
		 */
		if (st.st_mode & S_ISVTX)
			goto sticky_heaven; /* sticky */

		errno = EPERM;
		goto sticky_hell; /* not sticky */
	}

	/* for good measure */
	if (faccessat(dirfd, 	".", X_OK, AT_EACCESS) < 0)
		goto sticky_hell;

	/* non-world-writeable, so
	 * stickiness is do-not-care
	 */
	if (allow_noworld_unsticky)
		goto sticky_heaven; /* sticky! */

	goto sticky_hell; /* heaven visa denied */

sticky_heaven:
	close_on_eintr(&dirfd);
	errno = saved_errno;

	return 1;

sticky_hell:
	close_on_eintr(&dirfd);

	(void) set_errno(saved_errno, EPERM);
	return 0;
}

/* mk(h)temp - hardened mktemp.
 * like mkstemp, but (MUCH) harder.
 *
 * designed to resist TOCTOU attacks
 * e.g. directory race / symlink attack
 * 
 * extremely strict and even implements
 * some limited userspace-level sandboxing,
 * similar in spirit to openbsd unveil,
 * though unveil is from kernel space.
 *
 * supports both files and directories.
 * file: type = MKHTEMP_FILE (0)
 * dir: type = MKHTEMP_DIR (1)
 *
 * DESIGN NOTES:
 *
 * caller is expected to handle
 * cleanup e.g. free(), on *st,
 * *template, *fname (all of the
 * pointers). ditto fd cleanup.
 *
 * some limited cleanup is
 * performed here, e.g. directory/file
 * cleanup on error in mkhtemp_try_create
 *
 * we only check if these are not NULL,
 * and the caller is expected to take
 * care; without too many conditions,
 * these functions are more flexible,
 * but some precauttions are taken:
 *
 * when used via the function new_tmpfile
 * or new_tmpdir, thtis is extremely strict,
 * much stricter than previous mktemp
 * variants. for example, it is much
 * stricter about stickiness on world
 * writeable directories, and it enforces
 * file ownership under hardened mode
 * (only lets you touch your own files/dirs)
 */
/*
 TODO:
	some variables e.g. template vs suffix,
	assumes they match.
	we should test this explicitly,
	but the way this is called is
	currently safe - this would however
	be nice for future library use
	by outside projects.
	this whole code needs to be reorganised
*/
int
mkhtemp(int *fd,
    struct stat *st,
    char *template,
    int dirfd,
    const char *fname,
    struct stat *st_dir_initial,
    int type)
{
	size_t template_len = 0;
	size_t xc = 0;
	size_t fname_len = 0;

	char *fname_copy = NULL;
	char *p;

	size_t retries;

	int close_errno;
	int saved_errno = errno;

#if defined(PATH_LEN) && \
    (PATH_LEN) >= 256
	size_t max_len = PATH_LEN;
#else
	size_t max_len = 4096;
#endif
	int r;
	char *end;

	if (if_err(fd == NULL || template == NULL || fname == NULL ||
	          st_dir_initial == NULL, EFAULT) ||
	    if_err(*fd >= 0, EEXIST) ||
	    if_err(dirfd < 0, EBADF)
	      ||
	    if_err_sys(slen(template, max_len, &template_len) < 0) ||
	    if_err(template_len >= max_len, EMSGSIZE)
	      ||
	    if_err_sys(slen(fname, max_len, &fname_len) < 0) ||
	    if_err(fname == NULL, EINVAL) ||
	    if_err(strrchr(fname, '/') != NULL, EINVAL))
		return -1;

	for (end = template + template_len; /* count X */
	    end > template && *--end == 'X'; xc++);

	if (if_err(xc < 3 || xc > template_len, EINVAL) ||
	    if_err(fname_len > template_len, EOVERFLOW))
		return -1;

	if (if_err(memcmp(fname, template + template_len - fname_len,
	      fname_len) != 0, EINVAL))
		return -1;

	/* fname_copy = templatestr region only; p points to trailing XXXXXX */
	memcpy(smalloc(&fname_copy, fname_len + 1),
	    template + template_len - fname_len,
	    fname_len + 1);
	p = fname_copy + fname_len - xc;

	for (retries = 0; retries < MKHTEMP_RETRY_MAX; retries++) {

		r = mkhtemp_try_create(dirfd,
		    st_dir_initial, fname_copy,
		    p, xc, fd, st, type);

		if (r == 0)
			continue;
		if (r < 0)
			goto err;

		/* success: copy final name back */
		memcpy(template + template_len - fname_len,
		    fname_copy, fname_len);

		errno = saved_errno;
		goto success;
	}

	errno = EEXIST;

err:
	close_on_eintr(fd);

success:
	free_and_set_null(&fname_copy);

	return (*fd >= 0) ? *fd : -1;
}

int
mkhtemp_try_create(int dirfd,
    struct stat *st_dir_initial,
    char *fname_copy,
    char *p,
    size_t xc,
    int *fd,
    struct stat *st,
    int type)
{
	struct stat st_open;
	int saved_errno = errno;
	int rval = -1;
	char *rstr = NULL;

	int file_created = 0;
	int dir_created = 0;

	if (if_err(fd == NULL || st == NULL || p ==NULL || fname_copy ==NULL ||
	      st_dir_initial == NULL, EFAULT) ||
	    if_err(*fd >= 0, EEXIST))
		goto err;

	/* TODO: potential infinite loop under entropy failure.
	 * if attacker has control of rand - TODO: maybe add timeout
	 */
	memcpy(p, rstr = rchars(xc), xc);
	free_and_set_null(&rstr);

	if (if_err_sys(fd_verify_dir_identity(dirfd, st_dir_initial) < 0))
		goto err;

	if (type == MKHTEMP_FILE) {
#ifdef __linux__
		/* try O_TMPFILE fast path */
		if (mkhtemp_tmpfile_linux(dirfd,
		    st_dir_initial, fname_copy,
		    p, xc, fd, st) == 0) {

			errno = saved_errno;
			rval = 1;
			goto out;
		}
#endif

		*fd = openat2p(dirfd, fname_copy,
		    O_RDWR | O_CREAT | O_EXCL |
		    O_NOFOLLOW | O_CLOEXEC | O_NOCTTY, 0600);

		/* O_CREAT and O_EXCL guarantees creation upon success
		 */
		if (*fd >= 0)
			file_created = 1;

	} else { /* dir: MKHTEMP_DIR */

		if (mkdirat_on_eintr(dirfd, fname_copy, 0700) < 0)
			goto err;

		/* ^ NOTE: opening the directory here
			will never set errno=EEXIST,
			since we're not creating it */

		dir_created = 1;

		/* do it again (mitigate directory race) */
		if (fd_verify_dir_identity(dirfd, st_dir_initial) < 0)
			goto err;

		if ((*fd = openat2p(dirfd, fname_copy,
		    O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0)) < 0)
			goto err;

		if (if_err_sys(fstat(*fd, &st_open) < 0) ||
		    if_err(!S_ISDIR(st_open.st_mode), ENOTDIR))
			goto err;

		/* NOTE: pointless to check nlink here (only just opened) */
		if (fd_verify_dir_identity(dirfd, st_dir_initial) < 0)
			goto err;

	}

	/* NOTE: openat2p and mkdirat_on_eintr
	 * already handled EINTR/EAGAIN looping
	 */

	if (*fd < 0) {
		if (errno == EEXIST) {

			rval = 0;
			goto out;
		}
		goto err;
	}

	if (fstat(*fd, &st_open) < 0)
		goto err;

	if (type == MKHTEMP_FILE) {

		if (fd_verify_dir_identity(dirfd, st_dir_initial) < 0)
			goto err;

		if (secure_file(fd, st, &st_open,
		    O_APPEND, 1, 1, 0600) < 0) /* WARNING: only once */
			goto err;

	} else { /* dir: MKHTEMP_DIR */

		if (fd_verify_identity(*fd, &st_open, st_dir_initial) < 0)
			goto err;

		if (if_err(!S_ISDIR(st_open.st_mode), ENOTDIR) ||
		    if_err_sys(is_owner(&st_open) < 0) ||
		    if_err(st_open.st_mode & (S_IWGRP | S_IWOTH), EPERM))
			goto err;
	}

	errno = saved_errno;
	rval = 1;
	goto out;

err:
	close_on_eintr(fd);

	if (file_created)
		(void) unlinkat(dirfd, fname_copy, 0);
	if (dir_created)
		(void) unlinkat(dirfd, fname_copy, AT_REMOVEDIR);

	rval = -1;
out:
	return rval;
}

/* linux has its own special hardening
   available specifically for tmpfiles,
   which eliminates many race conditions.

   we still use openat() on bsd, which is
   still ok with our other mitigations
 */
#ifdef __linux__
int
mkhtemp_tmpfile_linux(int dirfd,
    struct stat *st_dir_initial,
    char *fname_copy,
    char *p,
    size_t xc,
    int *fd,
    struct stat *st)
{
	int saved_errno = errno;
	int tmpfd = -1;
	size_t retries;
	int linked = 0;
	char *rstr = NULL;

	if (fd == NULL || st == NULL ||
	    fname_copy == NULL || p == NULL ||
	    st_dir_initial == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* create unnamed tmpfile */
	tmpfd = openat(dirfd, ".",
	    O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);

	if (tmpfd < 0)
		return -1;

	if (fd_verify_dir_identity(dirfd, st_dir_initial) < 0)
		goto err;

	for (retries = 0; retries < MKHTEMP_RETRY_MAX; retries++) {

		memcpy(p, rstr = rchars(xc), xc);
		free_and_set_null(&rstr);

		if (fd_verify_dir_identity(dirfd,
		    st_dir_initial) < 0)
			goto err;

		if (linkat(tmpfd, "",
		    dirfd, fname_copy,
		    AT_EMPTY_PATH) == 0) {

			linked = 1; /* file created */

			if (fd_verify_dir_identity(dirfd, st_dir_initial) < 0)
				goto err;

			/* success */
			*fd = tmpfd;

			if (fstat(*fd, st) < 0)
				goto err;

			if (secure_file(fd, st, st,
			    O_APPEND, 1, 1, 0600) < 0)
				goto err;

			errno = saved_errno;
			return 0;
		}

		if (errno != EEXIST)
			goto err;

		/* retry on collision */
	}

	errno = EEXIST;

err:
	if (linked)
		(void) unlinkat(dirfd, fname_copy, 0);

	close_on_eintr(&tmpfd);
	return -1;
}
#endif

/* WARNING: **ONCE** per file.
 *
 * some of these checks will trip up
 * if you do them twice; all of them
 * only need to be done once anyway.
 */
int secure_file(int *fd,
    struct stat *st,
    struct stat *expected,
    int bad_flags,
    int check_seek,
    int do_lock,
    mode_t mode)
{
	int flags;
	struct stat st_now;
	int saved_errno = errno;

	if (if_err(fd == NULL || st == NULL, EFAULT) ||
	    if_err(*fd < 0, EBADF) ||
	    if_err_sys((flags = fcntl(*fd, F_GETFL)) == -1) ||
	    if_err(bad_flags > 0 && (flags & bad_flags), EPERM))
		goto err_demons;

	if (expected != NULL) {
		if (fd_verify_regular(*fd, expected, st) < 0)
			goto err_demons;
	} else if (if_err_sys(fstat(*fd, &st_now) == -1) ||
	    if_err(!S_ISREG(st_now.st_mode), EBADF)) {
		goto err_demons; /***********/
	} else                   /* ( >:3 ) */
		*st = st_now;    /*  /| |\  */  /* don't let him out */
				 /*   / \   */
	if (check_seek) {        /***********/
		if (lseek(*fd, 0, SEEK_CUR) == (off_t)-1)
			goto err_demons;
	} /* don't release the demon! */

	if (if_err(st->st_nlink != 1, ELOOP) ||
	    if_err(st->st_uid != geteuid() && geteuid() != 0, EPERM) ||
	    if_err_sys(is_owner(st) < 0) ||
	    if_err(st->st_mode & (S_IWGRP | S_IWOTH), EPERM))
		goto err_demons;

	if (do_lock) {
		if (lock_file(*fd, flags) == -1)
			goto err_demons;

		/* TODO: why would this be NULL? audit
		 * to find out. we should always verify! */
		if (expected != NULL)
			if (fd_verify_identity(*fd, expected, &st_now) < 0)
				goto err_demons;
	}

	if (fchmod(*fd, mode) == -1)
		goto err_demons;

	errno = saved_errno;
	return 0;

err_demons:
	return set_errno(saved_errno, EIO);
}

int
fd_verify_regular(int fd,
    const struct stat *expected,
    struct stat *out)
{if (
	if_err_sys(fd_verify_identity(fd, expected, out) < 0) ||
	    if_err(!S_ISREG(out->st_mode), EBADF)
	)	return -1;
	else
		return 0; /* regular file */
}

int
fd_verify_identity(int fd,
    const struct stat *expected,
    struct stat *out)
{
	struct stat st_now;
	int saved_errno = errno;

if(	if_err(fd < 0 || expected == NULL, EFAULT) ||
	  if_err_sys(fstat(fd, &st_now)) ||
	    if_err(st_now.st_dev != expected->st_dev ||
	      st_now.st_ino != expected->st_ino, ESTALE))
		return -1;

	if (out != NULL)
		*out = st_now;

	errno = saved_errno;
	return 0;
}

int
fd_verify_dir_identity(int fd,
    const struct stat *expected)
{
	struct stat st_now;
	int saved_errno = errno;

	if (if_err(fd < 0 || expected == NULL, EFAULT) ||
	    if_err_sys(fstat(fd, &st_now) < 0))
		return -1;

	if (st_now.st_dev != expected->st_dev ||
	    st_now.st_ino != expected->st_ino) {
		errno = ESTALE;
		return -1;
	}

	if (!S_ISDIR(st_now.st_mode)) {
		errno = ENOTDIR;
		return -1;
	}

	errno = saved_errno;
	return 0;
}

int
is_owner(struct stat *st)
{
	if (st == NULL) {

		errno = EFAULT;
		return -1;
	}

#if ALLOW_ROOT_OVERRIDE
	if (st->st_uid != geteuid() && /* someone else's file */
	    geteuid() != 0) { /* override for root */
#else
	if (st->st_uid != geteuid()) { /* someone else's file */
#endif				       /* and no root override */
		errno = EPERM;
		return -1;
	}

	return 0;
}

int
lock_file(int fd, int flags)
{
	struct flock fl;
	int saved_errno = errno;

	if (if_err(fd < 0, EBADF) ||
	    if_err(flags < 0, EINVAL))
		goto err_lock_file;

	memset(&fl, 0, sizeof(fl));

	if ((flags & O_ACCMODE) == O_RDONLY)
		fl.l_type = F_RDLCK;
	else
		fl.l_type = F_WRLCK;

	fl.l_whence = SEEK_SET;

	if (fcntl(fd, F_SETLK, &fl) == -1)
		goto err_lock_file;

	saved_errno = errno;
	return 0;

err_lock_file:
	return set_errno(saved_errno, EIO);
}
