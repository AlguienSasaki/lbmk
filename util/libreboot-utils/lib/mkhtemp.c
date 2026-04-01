/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Hardened mktemp (be nice to the demon).
 */

/* for openat2 / fast path: */
#ifdef __linux__
#if !defined(USE_OPENAT) || \
    ((USE_OPENAT) < 1) /* if 1: use openat, not openat2 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <sys/syscall.h>
#include <linux/openat2.h>
#ifndef O_TMPFILE
#define O_TMPFILE 020000000
#endif
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif
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
	struct stat st;

	const char *templatestr;

	size_t dirlen;
	char *dest = NULL; /* final path (will be written into "path") */
	int saved_errno = errno;
	int dirfd = -1;
	const char *fname = NULL;

	struct stat st_dir_first;

	char *fail_dir = NULL;

	errno = 0;

	if (if_err(path == NULL || fd == NULL, EFAULT) ||
	    if_err(*fd >= 0, EEXIST)) /* don't touch someone else's file */
		goto err;

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
	if (if_err(tmpdir ==NULL || *tmpdir == '\0' || *tmpdir != '/', EINVAL))
		goto err;

	if (template != NULL)
		templatestr = template;
	else
		templatestr = "tmp.XXXXXXXXXX";

       /* may as well calculate in advance */
	dirlen = slen(tmpdir, PATH_MAX, &dirlen);
	/* full path: */
	dest = scatn(3, (const char *[]) { tmpdir, "/", templatestr },
	    PATH_MAX, &dest);

	fname = dest + dirlen + 1;

	dirfd = fs_open(tmpdir,
	    O_RDONLY | O_DIRECTORY);
	if (dirfd < 0)
		goto err;

	if (fstat(dirfd, &st_dir_first) < 0)
		goto err;

	*fd = mkhtemp(fd, &st, dest, dirfd,
	    fname, &st_dir_first, type);
	if (*fd < 0)
		goto err;

	xclose(&dirfd);

	errno = saved_errno;
	*path = dest;

	reset_caller_errno(0);
	return 0;

err:
	free_and_set_null(&dest);

	xclose(&dirfd);
	xclose(fd);

	/* where a TMPDIR isn't found, and we err,
	 * we pass this back through for the
	 * error message
	 */
	if (fail_dir != NULL)
		*path = fail_dir;

	errno = saved_errno;
	return with_fallback_errno(EIO);
}


/* hardened TMPDIR parsing
 */

char *
env_tmpdir(int bypass_all_sticky_checks, char **tmpdir,
    char *override_tmpdir)
{
	char *t = NULL;
	int allow_noworld_unsticky;
	int saved_errno = errno;

	static const char tmp[] = "/tmp";
	static const char vartmp[] = "/var/tmp";

	char *rval = NULL;

	errno = 0;

	/* tmpdir is a user override, if set */
	if (override_tmpdir == NULL)
		t = getenv("TMPDIR");
	else
		t = override_tmpdir;

	if (t != NULL && *t != '\0') {

		if (tmpdir_policy(t,
		    &allow_noworld_unsticky) < 0)
			goto err;

		if (!world_writeable_and_sticky(t,
		    allow_noworld_unsticky,
		    bypass_all_sticky_checks))
			goto err;

		rval = t;
		goto out;
	}

	allow_noworld_unsticky = 0;

	if (world_writeable_and_sticky(tmp, allow_noworld_unsticky,
	    bypass_all_sticky_checks))
		rval = (char *)tmp;
	else if (world_writeable_and_sticky(vartmp,
	    allow_noworld_unsticky, bypass_all_sticky_checks))
		rval = (char *)vartmp;
	else
		goto err;

out:
	reset_caller_errno(0);
	if (tmpdir != NULL)
		*tmpdir = rval;
	return rval;
err:
	if (tmpdir != NULL && t != NULL)
		*tmpdir = t;
	(void) with_fallback_errno(EPERM);
	return NULL;
}

int
tmpdir_policy(const char *path,
    int *allow_noworld_unsticky)
{
	int saved_errno = errno;
	int r;
	errno = 0;

	if (if_err(path == NULL ||
	    allow_noworld_unsticky == NULL, EFAULT))
		goto err_tmpdir_policy;

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

	reset_caller_errno(0);
	return 0;

err_tmpdir_policy:
	return with_fallback_errno(EPERM);
}

int
same_dir(const char *a, const char *b)
{
	int fd_a = -1;
	int fd_b = -1;

	struct stat st_a;
	struct stat st_b;

	int saved_errno = errno;
	int rval = 0; /* LOGICAL error, 0, if 0 is returned */
	errno = 0;

	/* optimisation: if both dirs
	   are the same, we don't need
	   to check anything. sehr schnell!
	 */
	/* bonus: scmp checks null for us */
	if (!scmp(a, b, PATH_MAX, &rval))
		goto success_same_dir;
	else
		rval = 0; /* reset */

	if ((fd_a = fs_open(a, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) < 0 ||
	    (fd_b = fs_open(b, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) < 0 ||
	    fstat(fd_a, &st_a) < 0 ||
	    fstat(fd_b, &st_b) < 0)
		goto err_same_dir;

	if (st_a.st_dev == st_b.st_dev &&
	    st_a.st_ino == st_b.st_ino) {
success_same_dir:
		rval = 1; /* SUCCESS */
	}

	xclose(&fd_a);
	xclose(&fd_b);

	/* we reset caller errno regardless
	 * of success, so long as it's not
	 * a syscall error
	 */
	reset_caller_errno(0);
	return rval;

err_same_dir:
	/* FAILURE (probably syscall) - returns -1
	 */
	xclose(&fd_a);
	xclose(&fd_b);

	return with_fallback_errno(EIO); /* -1 */
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
	errno = 0;

	if (if_err(s == NULL || *s == '\0', EINVAL) ||
	    (dirfd = fs_open(s, O_RDONLY | O_DIRECTORY)) < 0 ||
	    fstat(dirfd, &st) < 0 ||
	    if_err(!S_ISDIR(st.st_mode), ENOTDIR))
		goto sticky_hell;

	/* *normal-**ish mode (libc):
	 */
	if (bypass_all_sticky_checks)
		goto sticky_heaven; /* normal == no security */

	/* extremely not-libc mode:
	 * only require stickiness on world-writeable dirs:
	 */
	if (st.st_mode & S_IWOTH) { /* world writeable */

		if (if_err(!(st.st_mode & S_ISVTX), EPERM))
			goto sticky_hell; /* not sticky */

		goto sticky_heaven; /* sticky! */
	} else if (allow_noworld_unsticky) {
		goto sticky_heaven; /* sticky visa */
	} else {
		goto sticky_hell; /* visa denied */
	}

sticky_heaven:
	if (faccessat(dirfd, 	".", X_OK, AT_EACCESS) < 0)
		goto sticky_hell; /* down you go! */

	xclose(&dirfd);
	reset_caller_errno(0);
	return 1;

sticky_hell:
	xclose(&dirfd);
	(void) with_fallback_errno(EPERM);
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
    struct stat *st_dir_first,
    int type)
{
	size_t template_len = 0;
	size_t xc = 0;
	size_t fname_len = 0;

	char *fname_copy = NULL;
	char *p;

	size_t retries;

	int saved_errno = errno;

	int r;
	char *end;

	errno = 0;

	if (if_err(fd == NULL || template == NULL || fname == NULL ||
	          st_dir_first == NULL, EFAULT) ||
	    if_err(*fd >= 0, EEXIST) ||
	    if_err(dirfd < 0, EBADF))
		goto err;

	/* count X */
	for (end = template + slen(template, PATH_MAX, &template_len);
	    end > template && *--end == 'X'; xc++);

	fname_len = slen(fname, PATH_MAX, &fname_len);
	if (if_err(strrchr(fname, '/') != NULL, EINVAL))
		goto err;

	if (if_err(xc < 3 || xc > template_len, EINVAL) ||
	    if_err(fname_len > template_len, EOVERFLOW))
		goto err;

	if (if_err(vcmp(fname, template + template_len - fname_len,
	      fname_len) != 0, EINVAL))
		goto err;

	/* fname_copy = templatestr region only; p points to trailing XXXXXX */
	memcpy(smalloc(&fname_copy, fname_len + 1),
	    template + template_len - fname_len,
	    fname_len + 1);
	p = fname_copy + fname_len - xc;

	for (retries = 0; retries < MKHTEMP_RETRY_MAX; retries++) {

		r = mkhtemp_try_create(dirfd,
		    st_dir_first, fname_copy,
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
	xclose(fd);
	free_and_set_null(&fname_copy);

	return with_fallback_errno(EIO);

success:
	free_and_set_null(&fname_copy);

	reset_caller_errno(0);
	return *fd;
}

int
mkhtemp_try_create(int dirfd,
    struct stat *st_dir_first,
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

	errno = 0;

	if (if_err(fd == NULL || st == NULL || p ==NULL || fname_copy ==NULL ||
	      st_dir_first == NULL, EFAULT) ||
	    if_err(*fd >= 0, EEXIST))
		goto err;

	/* TODO: potential infinite loop under entropy failure.
	 * if attacker has control of rand - TODO: maybe add timeout
	 */
	memcpy(p, rstr = rchars(xc), xc);
	free_and_set_null(&rstr);

	if (if_err_sys(fd_verify_dir_identity(dirfd, st_dir_first) < 0))
		goto err;

	if (type == MKHTEMP_FILE) {
#if defined(__linux__) && \
    (!defined(USE_OPENAT) || ((USE_OPENAT) < 1))
		/* try O_TMPFILE fast path */
		if (mkhtemp_tmpfile_linux(dirfd,
		    st_dir_first, fname_copy,
		    p, xc, fd, st) == 0) {

			errno = saved_errno;
			rval = 1;
			goto out;
		}
#endif

		*fd = openat_on_eintr(dirfd, fname_copy,
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
		if (fd_verify_dir_identity(dirfd, st_dir_first) < 0)
			goto err;

		if ((*fd = openat_on_eintr(dirfd, fname_copy,
		    O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0)) < 0)
			goto err;

		if (if_err_sys(fstat(*fd, &st_open) < 0) ||
		    if_err(!S_ISDIR(st_open.st_mode), ENOTDIR))
			goto err;

		/* NOTE: pointless to check nlink here (only just opened) */
		if (fd_verify_dir_identity(dirfd, st_dir_first) < 0)
			goto err;

	}

	/* NOTE: openat_on_eintr and mkdirat_on_eintr
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

		if (fd_verify_dir_identity(dirfd, st_dir_first) < 0)
			goto err;

		if (secure_file(fd, st, &st_open,
		    O_APPEND, 1, 1, 0600) < 0) /* WARNING: only once */
			goto err;

	} else { /* dir: MKHTEMP_DIR */

		if (fd_verify_identity(*fd, &st_open, st_dir_first) < 0)
			goto err;

		if (if_err(!S_ISDIR(st_open.st_mode), ENOTDIR) ||
		    if_err_sys(is_owner(&st_open) < 0) ||
		    if_err(st_open.st_mode & (S_IWGRP | S_IWOTH), EPERM))
			goto err;
	}

	rval = 1;

out:
	reset_caller_errno(0);
	return rval;
err:
	xclose(fd);

	if (file_created)
		(void) unlinkat(dirfd, fname_copy, 0);
	if (dir_created)
		(void) unlinkat(dirfd, fname_copy, AT_REMOVEDIR);

	return with_fallback_errno(EPERM);
}

/* linux has its own special hardening
   available specifically for tmpfiles,
   which eliminates many race conditions.

   we still use openat() on bsd, which is
   still ok with our other mitigations
 */
#if defined(__linux__) && \
    (!defined(USE_OPENAT) || ((USE_OPENAT) < 1))
int
mkhtemp_tmpfile_linux(int dirfd,
    struct stat *st_dir_first,
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
	errno = 0;

	if (if_err(fd == NULL || st == NULL ||
	    fname_copy == NULL || p == NULL ||
	    st_dir_first == NULL, EFAULT))
		goto err;

	/* create unnamed tmpfile */
	tmpfd = openat(dirfd, ".",
	    O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);

	if (tmpfd < 0)
		goto err;

	if (fd_verify_dir_identity(dirfd, st_dir_first) < 0)
		goto err;

	for (retries = 0; retries < MKHTEMP_RETRY_MAX; retries++) {

		memcpy(p, rstr = rchars(xc), xc);
		free_and_set_null(&rstr);

		if (fd_verify_dir_identity(dirfd,
		    st_dir_first) < 0)
			goto err;

		if (linkat(tmpfd, "", dirfd, fname_copy, AT_EMPTY_PATH) == 0) {

			linked = 1; /* file created */

			/* TODO: potential fd leak here.
			 * probably should only set *fd on successful
			 * return from this function (see below)
			 */
			if (fd_verify_dir_identity(dirfd, st_dir_first) < 0 ||
			    fstat(*fd = tmpfd, st) < 0 ||
			    secure_file(fd, st, st, O_APPEND, 1, 1, 0600) < 0)
				goto err;

			goto out;
		}

		if (errno != EEXIST)
			goto err;

		/* retry on collision */
	}

	errno = EEXIST;
err:
	if (linked)
		(void) unlinkat(dirfd, fname_copy, 0);

	xclose(&tmpfd);
	return with_fallback_errno(EIO);
out:
	reset_caller_errno(0);
	return 0;
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
	int flags = -1;
	struct stat st_now;
	int saved_errno = errno;
	errno = 0;

	if (if_err(fd == NULL || st == NULL, EFAULT) ||
	    if_err(*fd < 0, EBADF))
		goto err_demons;

	if ((flags = fcntl(*fd, F_GETFL)) == -1)
		goto err_demons;

	if (if_err(bad_flags > 0 && (flags & bad_flags), EPERM))
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

	reset_caller_errno(0);
	return 0;

err_demons:
	return with_fallback_errno(EIO);
}

int
fd_verify_regular(int fd,
    const struct stat *expected,
    struct stat *out)
{
	int saved_errno = errno;
	errno = 0;

	if (if_err_sys(fd_verify_identity(fd, expected, out) < 0) ||
	    if_err(!S_ISREG(out->st_mode), EBADF)) {
		return with_fallback_errno(EIO);
	} else {
		reset_caller_errno(0);
		return 0; /* regular file */
	}
}

int
fd_verify_identity(int fd,
    const struct stat *expected,
    struct stat *out)
{
	struct stat st_now;
	int saved_errno = errno;
	errno = 0;

if(	if_err(fd < 0 || expected == NULL, EFAULT) ||
	  if_err_sys(fstat(fd, &st_now)) ||
	    if_err(st_now.st_dev != expected->st_dev ||
	      st_now.st_ino != expected->st_ino, ESTALE))
		return with_fallback_errno(EIO);

	if (out != NULL)
		*out = st_now;

	reset_caller_errno(0);
	return 0;
}

int
fd_verify_dir_identity(int fd,
    const struct stat *expected)
{
	struct stat st_now;
	int saved_errno = errno;
	errno = 0;

	if (if_err(fd < 0 || expected == NULL, EFAULT) ||
	    if_err_sys(fstat(fd, &st_now) < 0) ||
	    if_err(st_now.st_dev != expected->st_dev, ESTALE) ||
	    if_err(st_now.st_ino != expected->st_ino, ESTALE) ||
	    if_err(!S_ISDIR(st_now.st_mode), ENOTDIR))
		goto err;

	reset_caller_errno(0);
	return 0;
err:
	return with_fallback_errno(EIO);
}

int
is_owner(struct stat *st)
{
	int saved_errno = errno;
	errno = 0;

	if (if_err(st == NULL, EFAULT) ||
	    if_err(st->st_uid != geteuid() /* someone else's file */
#if defined(ALLOW_ROOT_OVERRIDE) && ((ALLOW_ROOT_OVERRIDE) > 0)
	      && geteuid() != 0 /* override for root */
#endif
	    , EPERM)) return with_fallback_errno(EIO);

	reset_caller_errno(0);
	return 0;
}

int
lock_file(int fd, int flags)
{
	struct flock fl;
	int saved_errno = errno;
	int fcntl_rval = -1;
	errno = 0;

	if (if_err(fd < 0, EBADF) ||
	    if_err(flags < 0, EINVAL))
		goto err_lock_file;

	memset(&fl, 0, sizeof(fl));

	if ((flags & O_ACCMODE) == O_RDONLY)
		fl.l_type = F_RDLCK;
	else
		fl.l_type = F_WRLCK;

	fl.l_whence = SEEK_SET;

	if ((fcntl_rval = fcntl(fd, F_SETLK, &fl)) == -1)
		goto err_lock_file;

	reset_caller_errno(0);
	return 0;

err_lock_file:
	return with_fallback_errno(EIO);
}
