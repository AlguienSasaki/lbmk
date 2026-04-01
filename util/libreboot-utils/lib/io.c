/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * I/O functions specific to nvmutil.
 */

/* TODO: local tmpfiles not being deleted
	when flags==O_RDONLY e.g. dump command
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/common.h"

void
open_gbe_file(void)
{
	struct xstate *x = xstatus();
	struct commands *cmd = &x->cmd[x->i];
	struct xfile *f = &x->f;
	int saved_errno = errno;
	errno = 0;

	int _flags;

	f->gbe_fd = -1;

	open_file_on_eintr(f->fname, &f->gbe_fd,
	    O_NOFOLLOW | O_CLOEXEC | O_NOCTTY,
	    ((cmd->flags & O_ACCMODE) == O_RDONLY) ? 0400 : 0600,
	    &f->gbe_st);

	if (f->gbe_st.st_nlink > 1)
		exitf(
		    "%s: warning: file has multiple (%lu) hard links\n",
		    f->fname, (size_t)f->gbe_st.st_nlink);

	if (f->gbe_st.st_nlink == 0)
		exitf("%s: file unlinked while open", f->fname);

	if ((_flags = fcntl(f->gbe_fd, F_GETFL)) == -1)
		exitf("%s: fcntl(F_GETFL)", f->fname);

	/* O_APPEND allows POSIX write() to ignore
	 * the current write offset and write at EOF,
	 * which would break positional read/write
	 */

	if (_flags & O_APPEND)
		exitf("%s: O_APPEND flag", f->fname);

	f->gbe_file_size = f->gbe_st.st_size;

	switch (f->gbe_file_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		break;
	default:
		exitf("File size must be 8KB, 16KB or 128KB");
	}

/* currently fails (EBADF), locks are advisory anyway: */
/*
	if (lock_file(f->gbe_fd, cmd->flags) == -1)
		exitf("%s: can't lock", f->fname);
*/

	reset_caller_errno(0);
}

void
copy_gbe(void)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	read_file();

	if (f->gbe_file_size == SIZE_8KB)
		return;

	memcpy(f->buf + (size_t)GBE_PART_SIZE,
	    f->buf + (size_t)(f->gbe_file_size >> 1),
	    (size_t)GBE_PART_SIZE);
}

void
read_file(void)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	struct stat _st;
	ssize_t _r;

	/* read main file
	 */
	_r = rw_exact(f->gbe_fd, f->buf, f->gbe_file_size,
	    0, IO_PREAD);

	if (_r < 0)
		exitf("%s: read failed", f->fname);

	/* copy to tmpfile
	 */
	_r = rw_exact(f->tmp_fd, f->buf, f->gbe_file_size,
	    0, IO_PWRITE);

	if (_r < 0)
		exitf("%s: %s: copy failed",
		    f->fname, f->tname);

	/* file size comparison
	 */
	if (fstat(f->tmp_fd, &_st) == -1)
		exitf("%s: stat", f->tname);

	f->gbe_tmp_size = _st.st_size;

	if (f->gbe_tmp_size != f->gbe_file_size)
		exitf("%s: %s: not the same size",
		    f->fname, f->tname);

	/* needs sync, for verification
	 */
	if (fsync_on_eintr(f->tmp_fd) == -1)
		exitf("%s: fsync (tmpfile copy)", f->tname);

	_r = rw_exact(f->tmp_fd, f->bufcmp, f->gbe_file_size,
	    0, IO_PREAD);

	if (_r < 0)
		exitf("%s: read failed (cmp)", f->tname);

	if (vcmp(f->buf, f->bufcmp, f->gbe_file_size) != 0)
		exitf("%s: %s: read contents differ (pre-test)",
		    f->fname, f->tname);
}

void
write_gbe_file(void)
{
	struct xstate *x = xstatus();
	struct commands *cmd = &x->cmd[x->i];
	struct xfile *f = &x->f;

	size_t p;
	unsigned char update_checksum;

	if ((cmd->flags & O_ACCMODE) == O_RDONLY)
		return;

	if (same_file(f->tmp_fd, &f->tmp_st, 0) < 0)
		exitf("%s: file inode/device changed", f->tname);

	if (same_file(f->gbe_fd, &f->gbe_st, 1) < 0)
		exitf("%s: file has changed", f->fname);

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
rw_gbe_file_part(size_t p, int rw_type,
    const char *rw_type_str)
{
	struct xstate *x = xstatus();
	struct commands *cmd = &x->cmd[x->i];
	struct xfile *f = &x->f;

	ssize_t rval;

	off_t file_offset;

	size_t gbe_rw_size;
	unsigned char *mem_offset;

	gbe_rw_size = cmd->rw_size;

	if (rw_type < IO_PREAD || rw_type > IO_PWRITE)
		exitf("%s: %s: part %lu: invalid rw_type, %d",
		    f->fname, rw_type_str, (size_t)p, rw_type);

	mem_offset = gbe_mem_offset(p, rw_type_str);
	file_offset = (off_t)gbe_file_offset(p, rw_type_str);

	rval = rw_gbe_file_exact(f->tmp_fd, mem_offset,
	    gbe_rw_size, file_offset, rw_type);

	if (rval == -1)
		exitf("%s: %s: part %lu",
		    f->fname, rw_type_str, (size_t)p);

	if ((size_t)rval != gbe_rw_size)
		exitf("%s: partial %s: part %lu",
		    f->fname, rw_type_str, (size_t)p);
}

void
write_to_gbe_bin(void)
{
	struct xstate *x = xstatus();
	struct commands *cmd = &x->cmd[x->i];
	struct xfile *f = &x->f;

	int saved_errno;
	int mv;

	if ((cmd->flags & O_ACCMODE) != O_RDWR)
		return;

	write_gbe_file();

	/* We may otherwise read from
	 * cache, so we must sync.
	 */

	if (fsync_on_eintr(f->tmp_fd) == -1)
		exitf("%s: fsync (pre-verification)",
		    f->tname);

	check_written_part(0);
	check_written_part(1);

	report_io_err_rw();

	if (f->io_err_gbe)
		exitf("%s: bad write", f->fname);

	saved_errno = errno;

	xclose(&f->tmp_fd);
	xclose(&f->gbe_fd);

	errno = saved_errno;

	/* tmpfile written, now we
	 * rename it back to the main file
	 * (we do atomic writes)
	 */

	f->tmp_fd = -1;
	f->gbe_fd = -1;

	if (!f->io_err_gbe_bin) {

		mv = gbe_mv();

		if (mv < 0) {

			f->io_err_gbe_bin = 1;

			fprintf(stderr, "%s: %s\n",
			    f->fname, strerror(errno));
		} else {

			/* removed by rename
			 */
			free_and_set_null(&f->tname);
		}
	}

	if (!f->io_err_gbe_bin)
		return;

	fprintf(stderr, "FAIL (rename): %s: skipping fsync\n",
	    f->fname);
	if (errno)
		fprintf(stderr,
		    "errno %d: %s\n", errno, strerror(errno));
}

void
check_written_part(size_t p)
{
	struct xstate *x = xstatus();
	struct commands *cmd = &x->cmd[x->i];
	struct xfile *f = &x->f;

	ssize_t rval;

	size_t gbe_rw_size;

	off_t file_offset;
	unsigned char *mem_offset;

	unsigned char *buf_restore;

	if (!f->part_modified[p])
		return;

	gbe_rw_size = cmd->rw_size;

	mem_offset = gbe_mem_offset(p, "pwrite");
	file_offset = (off_t)gbe_file_offset(p, "pwrite");

	memset(f->pad, 0xff, sizeof(f->pad));

	if (same_file(f->tmp_fd, &f->tmp_st, 0) < 0)
		exitf("%s: file inode/device changed", f->tname);

	if (same_file(f->gbe_fd, &f->gbe_st, 1) < 0)
		exitf("%s: file changed during write", f->fname);

	rval = rw_gbe_file_exact(f->tmp_fd, f->pad,
	    gbe_rw_size, file_offset, IO_PREAD);

	if (rval == -1)
		f->rw_check_err_read[p] = f->io_err_gbe = 1;
	else if ((size_t)rval != gbe_rw_size)
		f->rw_check_partial_read[p] = f->io_err_gbe = 1;
	else if (vcmp(mem_offset, f->pad, gbe_rw_size) != 0)
		f->rw_check_bad_part[p] = f->io_err_gbe = 1;

	if (f->rw_check_err_read[p] ||
	    f->rw_check_partial_read[p])
		return;

	/* We only load one part on-file, into memory but
	 * always at offset zero, for post-write checks.
	 * That's why we hardcode good_checksum(0)
	 */

	buf_restore = f->buf;

	/* good_checksum works on f->buf
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
	struct xfile *f = &x->f;

	size_t p;

	if (!f->io_err_gbe)
		return;

	for (p = 0; p < 2; p++) {
		if (!f->part_modified[p])
			continue;

		if (f->rw_check_err_read[p])
			fprintf(stderr,
			    "%s: pread: p%lu (post-verification)\n",
			    f->fname, (size_t)p);
		if (f->rw_check_partial_read[p])
			fprintf(stderr,
			    "%s: partial pread: p%lu (post-verification)\n",
			    f->fname, (size_t)p);
		if (f->rw_check_bad_part[p])
			fprintf(stderr,
			    "%s: pwrite: corrupt write on p%lu\n",
			    f->fname, (size_t)p);

		if (f->rw_check_err_read[p] ||
		    f->rw_check_partial_read[p]) {
			fprintf(stderr,
			    "%s: p%lu: skipped checksum verification "
			    "(because read failed)\n",
			    f->fname, (size_t)p);

			continue;
		}

		fprintf(stderr, "%s: ", f->fname);

		if (f->post_rw_checksum[p])
			fprintf(stderr, "GOOD");
		else
			fprintf(stderr, "BAD");

		fprintf(stderr, " checksum in p%lu on-disk.\n",
		    (size_t)p);

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
	struct xfile *f = &x->f;

	int rval;

	int saved_errno;
	int tmp_gbe_bin_exists;

	/* will be set 0 if it doesn't
	 */
	tmp_gbe_bin_exists = 1;

	saved_errno = errno;

	rval = fs_rename_at(f->dirfd, f->tmpbase,
	    f->dirfd, f->base);

	if (rval > -1)
		tmp_gbe_bin_exists = 0;

	if (f->gbe_fd > -1) {
		xclose(&f->gbe_fd);

		if (fsync_dir(f->fname) < 0) {
			f->io_err_gbe_bin = 1;
			rval = -1;
		}
	}

	xclose(&f->tmp_fd);

	/* before this function is called,
	 * tmp_fd may have been moved
	 */
	if (tmp_gbe_bin_exists) {
		if (unlink(f->tname) < 0)
			rval = -1;
		else
			tmp_gbe_bin_exists = 0;
	}

	if (rval >= 0) 
		goto out;

	return with_fallback_errno(EIO);
out:
	reset_caller_errno(rval);
	return rval;
}

/* This one is similar to gbe_file_offset,
 * but used to check Gbe bounds in memory,
 * and it is *also* used during file I/O.
 */
unsigned char *
gbe_mem_offset(size_t p, const char *f_op)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	off_t gbe_off;

	gbe_off = gbe_x_offset(p, f_op, "mem",
	    GBE_PART_SIZE, GBE_WORK_SIZE);

	return (unsigned char *)
	    (f->buf + (size_t)gbe_off);
}

/* I/O operations filtered here. These operations must
 * only write from the 0th position or the half position
 * within the GbE file, and write 4KB of data.
 */
off_t
gbe_file_offset(size_t p, const char *f_op)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	off_t gbe_file_half_size;

	gbe_file_half_size = f->gbe_file_size >> 1;

	return gbe_x_offset(p, f_op, "file",
	    gbe_file_half_size, f->gbe_file_size);
}

off_t
gbe_x_offset(size_t p, const char *f_op, const char *d_type,
    off_t nsize, off_t ncmp)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	off_t off;

	check_bin(p, "part number");

	off = ((off_t)p) * (off_t)nsize;

	if (off > ncmp - GBE_PART_SIZE)
		exitf("%s: GbE %s %s out of bounds",
		    f->fname, d_type, f_op);

	if (off != 0 && off != ncmp >> 1)
		exitf("%s: GbE %s %s at bad offset",
		    f->fname, d_type, f_op);

	return off;
}

ssize_t
rw_gbe_file_exact(int fd, unsigned char *mem, size_t nrw,
    off_t off, int rw_type)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	ssize_t r;

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		return -1;

	if (mem != (void *)f->pad) {
		if (mem < f->buf)
			goto err_rw_gbe_file_exact;

		if ((size_t)(mem - f->buf) >= GBE_WORK_SIZE)
			goto err_rw_gbe_file_exact;
	}

	if (off < 0 || off >= f->gbe_file_size)
		goto err_rw_gbe_file_exact;

	if (nrw > (size_t)(f->gbe_file_size - off))
		goto err_rw_gbe_file_exact;

	if (nrw > (size_t)GBE_PART_SIZE)
		goto err_rw_gbe_file_exact;

	r = rw_exact(fd, mem, nrw, off, rw_type);

	return rw_over_nrw(r, nrw);

err_rw_gbe_file_exact:
	return with_fallback_errno(EIO);
}
