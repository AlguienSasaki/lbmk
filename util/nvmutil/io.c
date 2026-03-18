/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * I/O functions specific to nvmutil.
 *
 * Related: file.c
 */

#ifdef __OpenBSD__
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "include/common.h"

void
open_gbe_file(void)
{
	struct xstate *x = xstatus();
	struct commands *cmd;
	struct xfile *f;

	struct stat _st;
	int _flags;

	cmd = &x->cmd[x->i];
	f = &x->f;

	xopen(&f->gbe_fd, f->fname,
	    cmd->flags | O_BINARY |
	    O_NOFOLLOW | O_CLOEXEC, &_st);

	/* inode will be checked later on write */
	f->gbe_dev = _st.st_dev;
	f->gbe_ino = _st.st_ino;

	if (_st.st_nlink > 1)
		err(EINVAL,
		    "%s: warning: file has multiple (%lu) hard links\n",
		    f->fname, (unsigned long)_st.st_nlink);

	if (_st.st_nlink == 0)
		err(EIO, "%s: file unlinked while open", f->fname);

	_flags = fcntl(f->gbe_fd, F_GETFL);
	if (_flags == -1)
		err(errno, "%s: fcntl(F_GETFL)", f->fname);

	/*
	 * O_APPEND must not be used, because this
	 * allows POSIX write() to ignore the
	 * current write offset and write at EOF,
	 * which would therefore break pread/pwrite
	 */
	if (_flags & O_APPEND)
		err(EIO, "%s: O_APPEND flag", f->fname);

	f->gbe_file_size = _st.st_size;

	switch (f->gbe_file_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		break;
	default:
		err(EINVAL, "File size must be 8KB, 16KB or 128KB");
	}

	if (lock_file(f->gbe_fd, cmd->flags) == -1)
		err(errno, "%s: can't lock", f->fname);
}

/*
 * We copy the entire gbe file
 * to the tmpfile, and then we
 * work on that. We copy back
 * afterward. this is the copy.
 *
 * we copy to tmpfile even on
 * read-only commands, for the
 * double-read verification,
 * which also benefits cmd_cat.
 */
void
copy_gbe(void)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	f = &x->f;

	read_file();

	/*
	 regular operations post-read operate only on the first
	 8KB, because each GbE part is the first 4KB of each
	 half of the file.

	 we no longer care about anything past 8KB, until we get
	 to writing, at which point we will flush the buffer
	 again
	 */

	if (f->gbe_file_size == SIZE_8KB)
		return;

	x_v_memcpy(f->buf + (unsigned long)GBE_PART_SIZE,
	    f->buf + (unsigned long)(f->gbe_file_size >> 1),
	    (unsigned long)GBE_PART_SIZE);
}

void
read_file(void)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	struct stat _st;
	long _r;

	f = &x->f;

	/* read main file */
	_r = rw_file_exact(f->gbe_fd, f->buf, f->gbe_file_size,
	    0, IO_PREAD, NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (_r < 0)
		err(errno, "%s: read failed", f->fname);

	/* copy to tmpfile */

	_r = rw_file_exact(f->tmp_fd, f->buf, f->gbe_file_size,
	    0, IO_PWRITE, NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (_r < 0)
		err(errno, "%s: %s: copy failed",
		    f->fname, f->tname);

	/*
	 * file size comparison
	 */

	if (fstat(f->tmp_fd, &_st) == -1)
		err(errno, "%s: stat", f->tname);

	f->gbe_tmp_size = _st.st_size;

	if (f->gbe_tmp_size != f->gbe_file_size)
		err(EIO, "%s: %s: not the same size",
		    f->fname, f->tname);

	/*
	 * fsync tmp gbe file, because we will compare
	 * its contents to what was read (for safety)
	 */
	if (x_i_fsync(f->tmp_fd) == -1)
		err(errno, "%s: fsync (tmpfile copy)", f->tname);

	_r = rw_file_exact(f->tmp_fd, f->bufcmp, f->gbe_file_size,
	    0, IO_PREAD, NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (_r < 0)
		err(errno, "%s: read failed (cmp)", f->tname);

	if (x_i_memcmp(f->buf, f->bufcmp, f->gbe_file_size) != 0)
		err(errno, "%s: %s: read contents differ (pre-test)",
		    f->fname, f->tname);
}
void
write_gbe_file(void)
{
	struct xstate *x = xstatus();
	struct commands *cmd;
	struct xfile *f;

	struct stat _gbe_st;
	struct stat _tmp_st;

	unsigned long p;
	unsigned char update_checksum;

	cmd = &x->cmd[x->i];
	f = &x->f;

	if ((cmd->flags & O_ACCMODE) == O_RDONLY)
		return;

	if (fstat(f->gbe_fd, &_gbe_st) == -1)
		err(errno, "%s: re-check", f->fname);
	if (_gbe_st.st_dev != f->gbe_dev || _gbe_st.st_ino != f->gbe_ino)
		err(EIO, "%s: file replaced while open", f->fname);
	if (_gbe_st.st_size != f->gbe_file_size)
		err(errno, "%s: file size changed before write", f->fname);
	if (!S_ISREG(_gbe_st.st_mode))
		err(errno, "%s: file type changed before write", f->fname);

	if (fstat(f->tmp_fd, &_tmp_st) == -1)
		err(errno, "%s: re-check", f->tname);
	if (_tmp_st.st_dev != f->tmp_dev || _tmp_st.st_ino != f->tmp_ino)
		err(EIO, "%s: file replaced while open", f->tname);
	if (_tmp_st.st_size != f->gbe_file_size)
		err(errno, "%s: file size changed before write", f->tname);
	if (!S_ISREG(_tmp_st.st_mode))
		err(errno, "%s: file type changed before write", f->tname);

	update_checksum = cmd->chksum_write;

	for (p = 0; p < 2; p++) {
		if (!f->part_modified[p])
			continue;

		if (update_checksum)
			set_checksum(p);

		rw_gbe_file_part(p, IO_PWRITE, "pwrite");
	}
}

void
rw_gbe_file_part(unsigned long p, int rw_type,
    const char *rw_type_str)
{
	struct xstate *x = xstatus();
	struct commands *cmd;
	struct xfile *f;

	long rval;

	off_t file_offset;

	unsigned long gbe_rw_size;
	unsigned char *mem_offset;

	cmd = &x->cmd[x->i];
	f = &x->f;

	gbe_rw_size = cmd->rw_size;

	if (rw_type < IO_PREAD || rw_type > IO_PWRITE)
		err(errno, "%s: %s: part %lu: invalid rw_type, %d",
		    f->fname, rw_type_str, (unsigned long)p, rw_type);

	mem_offset = gbe_mem_offset(p, rw_type_str);
	file_offset = (off_t)gbe_file_offset(p, rw_type_str);

	rval = rw_gbe_file_exact(f->tmp_fd, mem_offset,
	    gbe_rw_size, file_offset, rw_type);

	if (rval == -1)
		err(errno, "%s: %s: part %lu",
		    f->fname, rw_type_str, (unsigned long)p);

	if ((unsigned long)rval != gbe_rw_size)
		err(EIO, "%s: partial %s: part %lu",
		    f->fname, rw_type_str, (unsigned long)p);
}

void
write_to_gbe_bin(void)
{
	struct xstate *x = xstatus();
	struct commands *cmd;
	struct xfile *f;

	int saved_errno;
	int mv;

	cmd = &x->cmd[x->i];
	f = &x->f;

	if ((cmd->flags & O_ACCMODE) != O_RDWR)
		return;

	write_gbe_file();

	/*
	 * We may otherwise read from
	 * cache, so we must sync.
	 */
	if (x_i_fsync(f->tmp_fd) == -1)
		err(errno, "%s: fsync (pre-verification)",
		    f->tname);

	check_written_part(0);
	check_written_part(1);

	report_io_err_rw();

	if (f->io_err_gbe)
		err(EIO, "%s: bad write", f->fname);

	/*
	 * success!
	 * now just rename the tmpfile
	 */

	saved_errno = errno;

	if (x_i_close(f->tmp_fd) == -1) {
		fprintf(stderr, "FAIL: %s: close\n", f->tname);
		f->io_err_gbe_bin = 1;
	}

	if (x_i_close(f->gbe_fd) == -1) {
		fprintf(stderr, "FAIL: %s: close\n", f->fname);
		f->io_err_gbe_bin = 1;
	}

	errno = saved_errno;

	f->tmp_fd = -1;
	f->gbe_fd = -1;

	if (!f->io_err_gbe_bin) {

		mv = gbe_mv();

		if (mv < 0) {

			f->io_err_gbe_bin = 1;

			fprintf(stderr, "%s: %s\n",
			    f->fname, strerror(errno));
		} else {
			/*
			 * tmpfile removed
			 * by the rename
			 */

			if (f->tname != NULL)
				free(f->tname);

			f->tname = NULL;
		}
	}

	/*
	 * finally:
	 * must sync to disk!
	 * very nearly done
	 */

	if (!f->io_err_gbe_bin)
		return;

	fprintf(stderr, "FAIL (rename): %s: skipping fsync\n",
	    f->fname);
	if (errno)
		fprintf(stderr,
		    "errno %d: %s\n", errno, strerror(errno));
}

void
check_written_part(unsigned long p)
{
	struct xstate *x = xstatus();
	struct commands *cmd;
	struct xfile *f;

	long rval;

	unsigned long gbe_rw_size;

	off_t file_offset;
	unsigned char *mem_offset;

	struct stat st;
	unsigned char *buf_restore;

	cmd = &x->cmd[x->i];
	f = &x->f;

	if (!f->part_modified[p])
		return;

	gbe_rw_size = cmd->rw_size;

	mem_offset = gbe_mem_offset(p, "pwrite");
	file_offset = (off_t)gbe_file_offset(p, "pwrite");

	memset(f->pad, 0xff, sizeof(f->pad));

	if (fstat(f->gbe_fd, &st) == -1)
		err(errno, "%s: fstat (post-write)", f->fname);
	if (st.st_dev != f->gbe_dev || st.st_ino != f->gbe_ino)
		err(EIO, "%s: file changed during write", f->fname);

	if (fstat(f->tmp_fd, &st) == -1)
		err(errno, "%s: fstat (post-write)", f->tname);
	if (st.st_dev != f->tmp_dev || st.st_ino != f->tmp_ino)
		err(EIO, "%s: file changed during write", f->tname);

	rval = rw_gbe_file_exact(f->tmp_fd, f->pad,
	    gbe_rw_size, file_offset, IO_PREAD);

	if (rval == -1)
		f->rw_check_err_read[p] = f->io_err_gbe = 1;
	else if ((unsigned long)rval != gbe_rw_size)
		f->rw_check_partial_read[p] = f->io_err_gbe = 1;
	else if (x_i_memcmp(mem_offset, f->pad, gbe_rw_size) != 0)
		f->rw_check_bad_part[p] = f->io_err_gbe = 1;

	if (f->rw_check_err_read[p] ||
	    f->rw_check_partial_read[p])
		return;

	/*
	 * We only load one part on-file, into memory but
	 * always at offset zero, for post-write checks.
	 * That's why we hardcode good_checksum(0).
	 */
	buf_restore = f->buf;

	/*
	 * good_checksum works on f->buf
	 * so let's change f->buf for now
	 */
	f->buf = f->pad;

	if (good_checksum(0))
		f->post_rw_checksum[p] = 1;

	f->buf = buf_restore;
}

void
report_io_err_rw(void)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	unsigned long p;

	f = &x->f;

	if (!f->io_err_gbe)
		return;

	for (p = 0; p < 2; p++) {
		if (!f->part_modified[p])
			continue;

		if (f->rw_check_err_read[p])
			fprintf(stderr,
			    "%s: pread: p%lu (post-verification)\n",
			    f->fname, (unsigned long)p);
		if (f->rw_check_partial_read[p])
			fprintf(stderr,
			    "%s: partial pread: p%lu (post-verification)\n",
			    f->fname, (unsigned long)p);
		if (f->rw_check_bad_part[p])
			fprintf(stderr,
			    "%s: pwrite: corrupt write on p%lu\n",
			    f->fname, (unsigned long)p);

		if (f->rw_check_err_read[p] ||
		    f->rw_check_partial_read[p]) {
			fprintf(stderr,
			    "%s: p%lu: skipped checksum verification "
			    "(because read failed)\n",
			    f->fname, (unsigned long)p);

			continue;
		}

		fprintf(stderr, "%s: ", f->fname);

		if (f->post_rw_checksum[p])
			fprintf(stderr, "GOOD");
		else
			fprintf(stderr, "BAD");

		fprintf(stderr, " checksum in p%lu on-disk.\n",
		    (unsigned long)p);

		if (f->post_rw_checksum[p]) {
			fprintf(stderr,
			    "    This does NOT mean it's safe. it may be\n"
			    "    salvageable if you use the cat feature.\n");
		}
	}
}

int
gbe_mv(void)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	int rval;

	int saved_errno;
	int tmp_gbe_bin_exists;

	char *dest_tmp;
	int dest_fd;

	f = &x->f;

	/* will be set 0 if it doesn't */
	tmp_gbe_bin_exists = 1;

	dest_tmp = NULL;
	dest_fd = -1;

	saved_errno = errno;

	rval = rename(f->tname, f->fname);

	if (rval > -1) {
		/*
		 * same filesystem
		 */

		tmp_gbe_bin_exists = 0;

		if (fsync_dir(f->fname) < 0) {
			f->io_err_gbe_bin = 1;
			rval = -1;
		}

		goto ret_gbe_mv;
	}

	if (errno != EXDEV)
		goto ret_gbe_mv;

	/* cross-filesystem rename */

	if ((rval = f->tmp_fd = open(f->tname,
	    O_RDONLY | O_BINARY)) == -1)
		goto ret_gbe_mv;

	/* create replacement temp in target directory */
	dest_tmp = new_tmpfile(&dest_fd, 1, f->fname);
	if (dest_tmp == NULL)
		goto ret_gbe_mv;

	/* copy data */

	rval = rw_file_exact(f->tmp_fd, f->bufcmp,
	    f->gbe_file_size, 0, IO_PREAD,
	    NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (rval < 0)
		goto ret_gbe_mv;

	rval = rw_file_exact(dest_fd, f->bufcmp,
	    f->gbe_file_size, 0, IO_PWRITE,
	    NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (rval < 0)
		goto ret_gbe_mv;

	if (x_i_fsync(dest_fd) == -1)
		goto ret_gbe_mv;

	if (x_i_close(dest_fd) == -1)
		goto ret_gbe_mv;

	if (rename(dest_tmp, f->fname) == -1)
		goto ret_gbe_mv;

	if (fsync_dir(f->fname) < 0) {
		f->io_err_gbe_bin = 1;
		goto ret_gbe_mv;
	}

	free(dest_tmp);
	dest_tmp = NULL;

ret_gbe_mv:

	if (f->gbe_fd > -1) {
		if (x_i_close(f->gbe_fd) < 0)
			rval = -1;
		if (fsync_dir(f->fname) < 0) {
			f->io_err_gbe_bin = 1;
			rval = -1;
		}
		f->gbe_fd = -1;
	}

	if (f->tmp_fd > -1) {
		if (x_i_close(f->tmp_fd) < 0)
			rval = -1;

		f->tmp_fd = -1;
	}

	/*
	 * before this function is called,
	 * tmp_fd may have been moved
	 */
	if (tmp_gbe_bin_exists) {
		if (unlink(f->tname) < 0)
			rval = -1;
		else
			tmp_gbe_bin_exists = 0;
	}

	if (rval < 0) {
		/*
		 * if nothing set errno,
		 * we assume EIO, or we
		 * use what was set
		 */
		if (errno == saved_errno)
			errno = EIO;
	} else {
		errno = saved_errno;
	}

	return rval;
}

/*
 * This one is similar to gbe_file_offset,
 * but used to check Gbe bounds in memory,
 * and it is *also* used during file I/O.
 */
unsigned char *
gbe_mem_offset(unsigned long p, const char *f_op)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	off_t gbe_off;

	f = &x->f;

	gbe_off = gbe_x_offset(p, f_op, "mem",
	    GBE_PART_SIZE, GBE_WORK_SIZE);

	return (unsigned char *)
	    (f->buf + (unsigned long)gbe_off);
}

/*
 * I/O operations filtered here. These operations must
 * only write from the 0th position or the half position
 * within the GbE file, and write 4KB of data.
 *
 * This check is called, to ensure just that.
 */
off_t
gbe_file_offset(unsigned long p, const char *f_op)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	off_t gbe_file_half_size;

	f = &x->f;

	gbe_file_half_size = f->gbe_file_size >> 1;

	return gbe_x_offset(p, f_op, "file",
	    gbe_file_half_size, f->gbe_file_size);
}

off_t
gbe_x_offset(unsigned long p, const char *f_op, const char *d_type,
    off_t nsize, off_t ncmp)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	off_t off;

	check_bin(p, "part number");

	f = &x->f;

	off = ((off_t)p) * (off_t)nsize;

	if (off > ncmp - GBE_PART_SIZE)
		err(ECANCELED, "%s: GbE %s %s out of bounds",
		    f->fname, d_type, f_op);

	if (off != 0 && off != ncmp >> 1)
		err(ECANCELED, "%s: GbE %s %s at bad offset",
		    f->fname, d_type, f_op);

	return off;
}

long
rw_gbe_file_exact(int fd, unsigned char *mem, unsigned long nrw,
    off_t off, int rw_type)
{
	struct xstate *x = xstatus();
	struct xfile *f;

	long r;

	f = &x->f;

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		return -1;

	if (mem != (void *)f->pad) {
		if (mem < f->buf)
			goto err_rw_gbe_file_exact;

		if ((unsigned long)(mem - f->buf) >= GBE_WORK_SIZE)
			goto err_rw_gbe_file_exact;
	}

	if (off < 0 || off >= f->gbe_file_size)
		goto err_rw_gbe_file_exact;

	if (nrw > (unsigned long)(f->gbe_file_size - off))
		goto err_rw_gbe_file_exact;

	if (nrw > (unsigned long)GBE_PART_SIZE)
		goto err_rw_gbe_file_exact;

	r = rw_file_exact(fd, mem, nrw, off, rw_type,
	    NO_LOOP_EAGAIN, LOOP_EINTR, MAX_ZERO_RW_RETRY,
	    OFF_ERR);

	return rw_over_nrw(r, nrw);

err_rw_gbe_file_exact:
	errno = EIO;
	return -1;
}
