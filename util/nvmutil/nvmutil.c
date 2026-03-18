/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 * Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com>
 *
 * This tool lets you modify Intel GbE NVM (Gigabit Ethernet
 * Non-Volatile Memory) images, e.g. change the MAC address.
 * These images configure your Intel Gigabit Ethernet adapter.
 *
 * This code is designed to be portable, running on as many
 * Unix and Unix-like systems as possible (mainly BSD/Linux).
 *
 * Recommended CFLAGS for Clang/GCC:
 *
 * -Os -Wall -Wextra -Werror -pedantic -std=c90
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

#include "nvmutil.h"

/*
 * Program state/command table
 * Default config stored here,
 * and copied to a newly allocated
 * buffer in memory, then the pointer
 * is passed. The rest of the program
 * will manipulate this data.
 */
struct xstate *
new_xstate(void)
{
	static struct xstate us = {
	/* .cmd (update cmd[] in the struct if adding to it)
	         DO NOT FORGET. or C will init zeroes/NULLs */

	/* cmd[] members */
		/* DO NOT MESS THIS UP */
		/* items must be set *exactly* */
	{
	{
		CMD_DUMP, "dump", cmd_helper_dump, ARGC_3,
		ARG_NOPART,
		SKIP_CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		NVM_SIZE, O_RDONLY
	}, {
		CMD_SETMAC, "setmac", cmd_helper_setmac, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, CHECKSUM_WRITE,
		NVM_SIZE, O_RDWR
	}, {
		CMD_SWAP, "swap", cmd_helper_swap, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDWR
	}, {
		CMD_COPY, "copy", cmd_helper_copy, ARGC_4,
		ARG_PART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDWR
	}, {
		CMD_CAT, "cat", cmd_helper_cat, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDONLY
	}, {
		CMD_CAT16, "cat16", cmd_helper_cat16, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDONLY
	}, {
		CMD_CAT128, "cat128", cmd_helper_cat128, ARGC_3,
		ARG_NOPART,
		CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
		GBE_PART_SIZE, O_RDONLY
	}
	},

	/* ->mac */
	{NULL, "xx:xx:xx:xx:xx:xx", {0, 0, 0}}, /* .str, .rmac, .mac_buf */

	/* .f */
	{0},

	/* .argv0 (for our getprogname implementation) */
	NULL,

	/* ->i   (index to cmd[]) */
	0,

	/* .no_cmd (set 0 when a command is found) */
	1,

	/* .xsize (size of the stuct will be stored here later) */
	0,

	/* .cat (cat helpers set this) */
	-1

	};

	struct xstate *xs_new;

	us.xsize = sizeof(us);

	xs_new = malloc(us.xsize);
	if (xs_new == NULL)
		err(ECANCELED, "Could not initialise new state");

	memcpy(xs_new, &us, us.xsize);

	/*
	 * Some pointers should be set
	 * in the copy, not the template,
	 * if they point to other items
	 * in the struct, because if they
	 * were set before copy, then copied,
	 * the copied pointer would be invalid,
	 * referring to the reference struct
	 *
	 * e.g. ->f.buf (gbe tmp file work memory):
	 */

	xs_new->f.buf = xs_new->f.real_buf;

	xs_new->f.gbe_fd = -1;
	xs_new->f.tmp_fd = -1;

	xs_new->f.tname = NULL;
	xs_new->f.fname = NULL;

	return xs_new;
}

static struct xstate *x = NULL;

int
main(int argc, char *argv[])
{
	struct commands *cmd;
	struct xfile *f;
	unsigned long i;

	char *tmp_path;

	struct stat st;
	int fd;

	tmp_path = NULL;

#ifndef S_ISREG
	err(ECANCELED, "Can't determine file types (S_ISREG undefined)");
#endif

#ifndef CHAR_BIT
	err(ECANCELED, "Unknown char size");
#else
	if (CHAR_BIT != 8)
		err(EINVAL, "Unsupported char size");
#endif

	if (argc < 3)
		usage();

#ifdef NVMUTIL_UNVEIL
	/*
	 * if global tmp is a different filesystem,
	 * unveil would trap on final file rename
	 * and we can't know the path in advance
	 */
	tmp_path = new_tmpfile(&fd, 1, NULL);
#else
	tmp_path = new_tmpfile(&fd, 0, NULL);
#endif

	if (tmp_path == NULL)
		err(errno, "Can't create tmpfile");

#ifdef NVMUTIL_PLEDGE
#ifdef NVMUTIL_UNVEIL
	if (pledge("stdio flock rpath wpath cpath unveil", NULL) == -1)
		err(errno, "pledge, unveil");
	if (unveil("/dev/null", "r") == -1)
		err(errno, "unveil: /dev/null");
#else
	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		err(errno, "pledge");
#endif
#endif

	x = new_xstate();

	if (x == NULL)
		err(errno, NULL);
	if (x->f.buf == NULL)
		err(EINVAL, "Work buffer not initialised");

	x->argv0 = argv[0];
	f = &x->f;

	f->fname = argv[1];
	f->tname = tmp_path;
	f->tmp_fd = fd;

	if(fstat(fd, &st) < 0)
		err(errno, "can't stat tmpfile");

	f->tmp_dev = st.st_dev;
	f->tmp_ino = st.st_ino;

	sanitize_command_list();

	set_cmd(argc, argv);
	set_cmd_args(argc, argv);

	i = x->i;
	cmd = &x->cmd[i];

#ifdef NVMUTIL_UNVEIL
	if ((cmd->flags & O_ACCMODE) == O_RDONLY) {
		if (unveil(f->fname, "r") == -1)
			err(errno, "%s: unveil r", f->fname);
	} else {
		if (unveil(f->tname, "rwc") == -1)
			err(errno, "%s: unveil rw", f->tname);
	}

	if (unveil(f->tname, "rwc") == -1)
		err(errno, "%s: unveil rwc", f->tname);

	if (unveil(NULL, NULL) == -1)
		err(errno, "unveil block (rw)");

	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		err(errno, "pledge (kill unveil)");
#endif

	open_gbe_file();

	memset(f->buf, 0, GBE_BUF_SIZE);
	memset(f->bufcmp, 0, GBE_BUF_SIZE);

	copy_gbe();

	read_checksums();

	run_cmd();

	if ((cmd->flags & O_ACCMODE) == O_RDWR)
		write_to_gbe_bin();

	if (exit_cleanup() == -1)
		err(EIO, "%s: close", f->fname);

	if (f->io_err_gbe_bin)
		err(EIO, "%s: error writing final file");

	if (f->tname != NULL)
		free(f->tname);

	return EXIT_SUCCESS;
}

/*
 * Guard against regressions by maintainers (command table)
 */
void
sanitize_command_list(void)
{
	unsigned long c;
	unsigned long num_commands;

	check_null_command("sanitize_command_list");

	num_commands = items(x->cmd);

	for (c = 0; c < num_commands; c++)
		sanitize_command_index(c);
}

/*
 * TODO: specific config checks per command
 */
void
sanitize_command_index(unsigned long c)
{
	struct commands *cmd;

	int _flag;
	unsigned long gbe_rw_size;

	check_null_command("sanitize_command_index");

	cmd = &x->cmd[c];

	check_command_num(c);

	if (cmd->argc < 3)
		err(EINVAL, "cmd index %lu: argc below 3, %d",
		    (unsigned long)c, cmd->argc);

	if (cmd->str == NULL)
		err(EINVAL, "cmd index %lu: NULL str",
		    (unsigned long)c);

	if (*cmd->str == '\0')
		err(EINVAL, "cmd index %lu: empty str",
		    (unsigned long)c);

	if (xstrxlen(cmd->str, MAX_CMD_LEN + 1) >
	    MAX_CMD_LEN) {
		err(EINVAL, "cmd index %lu: str too long: %s",
		    (unsigned long)c, cmd->str);
	}

	if (cmd->run == NULL)
		err(EINVAL, "cmd index %lu: cmd ptr null",
		    (unsigned long)c);

	check_bin(cmd->arg_part, "cmd.arg_part");
	check_bin(cmd->chksum_read, "cmd.chksum_read");
	check_bin(cmd->chksum_write, "cmd.chksum_write");

	gbe_rw_size = cmd->rw_size;

	switch (gbe_rw_size) {
	case GBE_PART_SIZE:
	case NVM_SIZE:
		break;
	default:
		err(EINVAL, "Unsupported rw_size: %lu",
		    (unsigned long)gbe_rw_size);
	}

	if (gbe_rw_size > GBE_PART_SIZE)
		err(EINVAL, "rw_size larger than GbE part: %lu",
		    (unsigned long)gbe_rw_size);

	_flag = (cmd->flags & O_ACCMODE);

	if (_flag != O_RDONLY &&
	    _flag != O_RDWR)
		err(EINVAL, "invalid cmd.flags setting");
}

void
set_cmd(int argc, char *argv[])
{
	const char *cmd;

	unsigned long c;

	check_null_command("set_cmd");

	for (c = 0; c < items(x->cmd); c++) {

		cmd = x->cmd[c].str;

		/* not the right command */
		if (xstrxcmp(argv[2], cmd, MAX_CMD_LEN) != 0)
			continue;

		/* valid command found */
		if (argc >= x->cmd[c].argc) {
			x->no_cmd = 0;
			x->i = c; /* set command */

			return;
		}

		err(EINVAL,
		    "Too few args on command '%s'", cmd);
	}

	x->no_cmd = 1;
}

void
set_cmd_args(int argc, char *argv[])
{
	struct commands *cmd;
	struct xfile *f;
	unsigned long i;

	check_null_command("set_cmd_args");

	i = x->i;
	cmd = &x->cmd[i];
	f = &x->f;

	if (!valid_command(i) || argc < 3)
		usage();

	if (x->no_cmd)
		usage();

	/* Maintainer bugs */
	if (cmd->arg_part && argc < 4)
		err(EINVAL,
		    "arg_part set for command that needs argc4");

	if (cmd->arg_part && i == CMD_SETMAC)
		err(EINVAL,
		    "arg_part set on CMD_SETMAC");

	if (i == CMD_SETMAC) {

		if (argc >= 4)
			x->mac.str = argv[3];
		else
			x->mac.str = x->mac.rmac;

	} else if (cmd->arg_part) {

		f->part = conv_argv_part_num(argv[3]);
	}
}

unsigned long
conv_argv_part_num(const char *part_str)
{
	unsigned char ch;

	if (part_str[0] == '\0' || part_str[1] != '\0')
		err(EINVAL, "Partnum string '%s' wrong length", part_str);

	/* char signedness is implementation-defined */
	ch = (unsigned char)part_str[0];
	if (ch < '0' || ch > '1')
		err(EINVAL, "Bad part number (%c)", ch);

	return (unsigned long)(ch - '0');
}

/*
 * Portable strcmp() but blocks NULL/empty/unterminated
 * strings. Even stricter than strncmp().
 */
int
xstrxcmp(const char *a, const char *b, unsigned long maxlen)
{
	unsigned long i;

	if (a == NULL || b == NULL)
		err(EINVAL, "NULL input to xstrxcmp");

	if (*a == '\0' || *b == '\0')
		err(EINVAL, "Empty string in xstrxcmp");

	for (i = 0; i < maxlen; i++) {

		unsigned char ac = (unsigned char)a[i];
		unsigned char bc = (unsigned char)b[i];

		if (ac == '\0' || bc == '\0') {
			if (ac == bc)
				return 0;
			return ac - bc;
		}

		if (ac != bc)
			return ac - bc;
	}

	/*
	 * We reached maxlen, so assume unterminated string.
	 */
	err(EINVAL, "Unterminated string in xstrxcmp");

	/*
	 * Should never reach here. This keeps compilers happy.
	 */
	errno = EINVAL;
	return -1;
}

void
open_gbe_file(void)
{
	struct commands *cmd;
	struct xfile *f;

	struct stat _st;
	int _flags;

	check_null_command("open_gbe_file");

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

/*
 * TODO: make generic. S_ISREG: check every other
 * type, erring only if it doesn't match what was
 * passed as type requested.
 * also:
 * have variable need_seek, only err on seek if
 * need_seek is set.
 * also consider the stat check in this generic
 * context
 * make tthe return type an int, not a void.
 * return -1 with errno set to indicate error,
 * though the syscalls mostly handle that.
 * save errno before lseek, resetting it after
 * the check if return >-1
 */
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
	struct xfile *f;

	check_null_command("copy_gbe");

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
	struct xfile *f;

	struct stat _st;
	long _r;

	check_null_command("read_file");

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
read_checksums(void)
{
	struct commands *cmd;
	struct xfile *f;

	unsigned long _p;
	unsigned long _skip_part;

	unsigned char _num_invalid;
	unsigned char _max_invalid;

	check_null_command("read_checksums");

	cmd = &x->cmd[x->i];
	f = &x->f;

	f->part_valid[0] = 0;
	f->part_valid[1] = 0;

	if (!cmd->chksum_read)
		return;

	_num_invalid = 0;
	_max_invalid = 2;

	if (cmd->arg_part)
		_max_invalid = 1;

	/*
	 * Skip verification on this part,
	 * but only when arg_part is set.
	 */
	_skip_part = f->part ^ 1;

	for (_p = 0; _p < 2; _p++) {
		/*
		 * Only verify a part if it was *read*
		 */
		if (cmd->arg_part && (_p == _skip_part))
			continue;

		f->part_valid[_p] = good_checksum(_p);
		if (!f->part_valid[_p])
			++_num_invalid;
	}

	if (_num_invalid >= _max_invalid) {
		if (_max_invalid == 1)
			err(ECANCELED, "%s: part %lu has a bad checksum",
			    f->fname, (unsigned long)f->part);
		err(ECANCELED, "%s: No valid checksum found in file",
		    f->fname);
	}
}

int
good_checksum(unsigned long partnum)
{
	unsigned short expected_checksum;
	unsigned short actual_checksum;

	check_null_command("good_checksum");

	expected_checksum =
	    calculated_checksum(partnum);

	actual_checksum =
	    nvm_word(NVM_CHECKSUM_WORD, partnum);

	if (expected_checksum == actual_checksum) {
		return 1;
	} else {
		return 0;
	}
}

void
run_cmd(void)
{
	unsigned long i;
	void (*run)(void);

	check_null_command("run_cmd");

	i = x->i;
	run = x->cmd[i].run;

	check_command_num(i);

	if (run == NULL)
		err(EINVAL, "Command %lu: null ptr", i);

	run();

	for (i = 0; i < items(x->cmd); i++)
		x->cmd[i].run = cmd_helper_err;
}

void
check_command_num(unsigned long c)
{
	check_null_command("check_command_num");

	if (!valid_command(c))
		err(EINVAL, "Invalid run_cmd arg: %lu",
		    (unsigned long)c);
}

unsigned char
valid_command(unsigned long c)
{
	struct commands *cmd;

	check_null_command("valid_command");

	if (c >= items(x->cmd))
		return 0;

	cmd = &x->cmd[c];

	if (c != cmd->chk)
		err(EINVAL,
		    "Invalid cmd chk value (%lu) vs arg: %lu",
		    cmd->chk, c);

	return 1;
}

void
cmd_helper_setmac(void)
{
	unsigned long partnum;
	struct macaddr *mac;

	check_null_command("cmd_helper_setmac");

	mac = &x->mac;

	check_cmd(cmd_helper_setmac, "setmac");

	printf("MAC address to be written: %s\n", mac->str);
	parse_mac_string();

	for (partnum = 0; partnum < 2; partnum++)
		write_mac_part(partnum);
}

void
parse_mac_string(void)
{
	struct macaddr *mac;

	unsigned long mac_byte;

	check_null_command("parse_mac_string");

	mac = &x->mac;

	if (xstrxlen(x->mac.str, 18) != 17)
		err(EINVAL, "MAC address is the wrong length");

	memset(mac->mac_buf, 0, sizeof(mac->mac_buf));

	for (mac_byte = 0; mac_byte < 6; mac_byte++)
		set_mac_byte(mac_byte);

	if ((mac->mac_buf[0] | mac->mac_buf[1] | mac->mac_buf[2]) == 0)
		err(EINVAL, "Must not specify all-zeroes MAC address");

	if (mac->mac_buf[0] & 1)
		err(EINVAL, "Must not specify multicast MAC address");
}

/*
 * strnlen() but aborts on NULL input, and empty strings.
 * Our version also prohibits unterminated strings.
 * strnlen() was standardized in POSIX.1-2008 and is not
 * available on some older systems, so we provide our own.
 */
unsigned long
xstrxlen(const char *scmp, unsigned long maxlen)
{
	unsigned long xstr_index;

	if (scmp == NULL)
		err(EINVAL, "NULL input to xstrxlen");

	if (*scmp == '\0')
		err(EINVAL, "Empty string in xstrxlen");

	for (xstr_index = 0;
	    xstr_index < maxlen && scmp[xstr_index] != '\0';
	    xstr_index++);

	if (xstr_index == maxlen)
		err(EINVAL, "Unterminated string in xstrxlen");

	return xstr_index;
}

void
set_mac_byte(unsigned long mac_byte_pos)
{
	struct macaddr *mac;

	char separator;

	unsigned long mac_str_pos;
	unsigned long mac_nib_pos;

	check_null_command("set_mac_byte");

	mac = &x->mac;

	mac_str_pos = mac_byte_pos * 3;

	if (mac_str_pos < 15) {
		if ((separator = mac->str[mac_str_pos + 2]) != ':')
			err(EINVAL, "Invalid MAC address separator '%c'",
			    separator);
	}

	for (mac_nib_pos = 0; mac_nib_pos < 2; mac_nib_pos++)
		set_mac_nib(mac_str_pos, mac_byte_pos, mac_nib_pos);
}

void
set_mac_nib(unsigned long mac_str_pos,
    unsigned long mac_byte_pos, unsigned long mac_nib_pos)
{
	struct macaddr *mac;

	char mac_ch;
	unsigned short hex_num;

	check_null_command("sanitize_command_list");

	mac = &x->mac;

	mac_ch = mac->str[mac_str_pos + mac_nib_pos];

	if ((hex_num = hextonum(mac_ch)) > 15)
		err(EINVAL, "Invalid character '%c'",
		    mac->str[mac_str_pos + mac_nib_pos]);

	/*
	 * If random, ensure that local/unicast bits are set.
	 */
	if ((mac_byte_pos == 0) && (mac_nib_pos == 1) &&
	    ((mac_ch | 0x20) == 'x' ||
	    (mac_ch == '?')))
		hex_num = (hex_num & 0xE) | 2; /* local, unicast */

	/*
	 * MAC words stored big endian in-file, little-endian
	 * logically, so we reverse the order.
	 */
	mac->mac_buf[mac_byte_pos >> 1] |= hex_num <<
	    (((mac_byte_pos & 1) << 3) /* left or right byte? */
	    | ((mac_nib_pos ^ 1) << 2)); /* left or right nib? */
}

unsigned short
hextonum(char ch_s)
{
	unsigned char ch;

	ch = (unsigned char)ch_s;

	if ((unsigned int)(ch - '0') <= 9)
		return ch - '0';

	ch |= 0x20;

	if ((unsigned int)(ch - 'a') <= 5)
		return ch - 'a' + 10;

	if (ch == '?' || ch == 'x')
		return (unsigned short)rlong() & 0xf;

	return 16; /* invalid character */
}

unsigned long
rlong(void)
{
#if defined(__OpenBSD__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__)

	unsigned long rval;
	arc4random_buf(&rval, sizeof(unsigned long);

	return rval;
#else
	int fd;

	long nr;

	unsigned long rval;

	fd = open("/dev/urandom", O_RDONLY | O_BINARY);

#if !(defined(__OpenBSD__) && defined(OpenBSD)) || \
    (defined(__OpenBSD__) && defined(OpenBSD) && \
    OpenBSD < 604)
	if (fd < 0) /* old openbsd */
		fd = open("/dev/arandom", O_RDONLY | O_BINARY);
#endif

	if (fd < 0)
		fd = open("/dev/random", O_RDONLY | O_BINARY);

	if (fd < 0)
		err(errno, "can't open random device");

	nr = rw_file_exact(fd, (unsigned char *)&rval,
	    sizeof(unsigned long), 0, IO_READ, LOOP_EAGAIN,
	    LOOP_EINTR, MAX_ZERO_RW_RETRY, OFF_ERR);

	if (x_i_close(fd) < 0)
		err(errno, "Can't close randomness fd");

	if (nr != sizeof(unsigned long))
		err(errno, "Incomplete read from random device");

	return rval;
#endif
}

void
write_mac_part(unsigned long partnum)
{
	struct xfile *f;
	struct macaddr *mac;

	unsigned long w;

	check_null_command("write_mac_part");

	f = &x->f;
	mac = &x->mac;

	check_bin(partnum, "part number");
	if (!f->part_valid[partnum])
		return;

	for (w = 0; w < 3; w++)
		set_nvm_word(w, partnum, mac->mac_buf[w]);

	printf("Wrote MAC address to part %lu: ",
	    (unsigned long)partnum);
	print_mac_from_nvm(partnum);
}

void
cmd_helper_dump(void)
{
	struct xfile *f;

	unsigned long p;

	check_null_command("cmd_helper_dump");

	f = &x->f;

	check_cmd(cmd_helper_dump, "dump");

	f->part_valid[0] = good_checksum(0);
	f->part_valid[1] = good_checksum(1);

	for (p = 0; p < 2; p++) {

		if (!f->part_valid[p]) {

			fprintf(stderr,
			    "BAD checksum %04x in part %lu (expected %04x)\n",
			    nvm_word(NVM_CHECKSUM_WORD, p),
			    (unsigned long)p,
			    calculated_checksum(p));
		}

		printf("MAC (part %lu): ",
		    (unsigned long)p);

		print_mac_from_nvm(p);

		hexdump(p);
	}
}

void
print_mac_from_nvm(unsigned long partnum)
{
	unsigned long c;
	unsigned short val16;

	check_null_command("print_mac_from_nvm");

	for (c = 0; c < 3; c++) {
		val16 = nvm_word(c, partnum);
		printf("%02x:%02x",
		    (unsigned int)(val16 & 0xff),
		    (unsigned int)(val16 >> 8));
		if (c == 2)
			printf("\n");
		else
			printf(":");
	}
}

void
hexdump(unsigned long partnum)
{
	unsigned long c;
	unsigned long row;
	unsigned short val16;

	check_null_command("hexdump");

	for (row = 0; row < 8; row++) {

		printf("%08lx ",
		    (unsigned long)((unsigned long)row << 4));

		for (c = 0; c < 8; c++) {

			val16 = nvm_word((row << 3) + c, partnum);

			if (c == 4)
				printf(" ");

			printf(" %02x %02x",
			    (unsigned int)(val16 & 0xff),
			    (unsigned int)(val16 >> 8));

		}

		printf("\n");
	}
}

void
cmd_helper_swap(void)
{
	struct xfile *f;

	check_null_command("cmd_helper_swap");

	check_cmd(cmd_helper_swap, "swap");

	f = &x->f;

	x_v_memcpy(
	    f->buf + (unsigned long)GBE_WORK_SIZE,
	    f->buf,
	    GBE_PART_SIZE);

	x_v_memcpy(
	    f->buf,
	    f->buf + (unsigned long)GBE_PART_SIZE,
	    GBE_PART_SIZE);

	x_v_memcpy(
	    f->buf + (unsigned long)GBE_PART_SIZE,
	    f->buf + (unsigned long)GBE_WORK_SIZE,
	    GBE_PART_SIZE);

	set_part_modified(0);
	set_part_modified(1);
}

void
cmd_helper_copy(void)
{
	struct xfile *f;

	check_null_command("cmd_helper_copy");

	check_cmd(cmd_helper_copy, "copy");

	f = &x->f;

	x_v_memcpy(
	    f->buf + (unsigned long)((f->part ^ 1) * GBE_PART_SIZE),
	    f->buf + (unsigned long)(f->part * GBE_PART_SIZE),
	    GBE_PART_SIZE);

	set_part_modified(f->part ^ 1);
}

void
cmd_helper_cat(void)
{
	check_null_command("cmd_helper_cat");

	check_cmd(cmd_helper_cat, "cat");

	x->cat = 0;
	cat(0);
}

void
cmd_helper_cat16(void)
{
	check_null_command("cmd_helper_cat16");

	check_cmd(cmd_helper_cat16, "cat16");

	x->cat = 1;
	cat(1);
}

void
cmd_helper_cat128(void)
{
	check_null_command("cmd_helper_cat128");

	check_cmd(cmd_helper_cat128, "cat128");

	x->cat = 15;
	cat(15);
}

void
check_cmd(void (*fn)(void),
    const char *name)
{
	unsigned long i;

	check_null_command("check_cmd");

	if (x->cmd[x->i].run != fn)
		err(ECANCELED, "Running %s, but cmd %s is set",
		    name, x->cmd[x->i].str);

	/*
	 * In addition to making sure we ran
	 * the right command, we now disable
	 * all commands from running again
	 *
	 * the _nop function will just call
	 * err() immediately
	 */

	for (i = 0; i < items(x->cmd); i++)
		x->cmd[i].run = cmd_helper_err;
}

void
cmd_helper_err(void)
{
	err(ECANCELED,
	    "Erroneously running command twice");
}

void
cat(unsigned long nff)
{
	struct xfile *f;

	unsigned long p;
	unsigned long ff;

	check_null_command("cat");

	f = &x->f;

	p = 0;
	ff = 0;

	if ((unsigned long)x->cat != nff) {

		err(ECANCELED, "erroneous call to cat");
	}

	fflush(NULL);

	memset(f->pad, 0xff, GBE_PART_SIZE);

	for (p = 0; p < 2; p++) {

		cat_buf(f->bufcmp +
		    (unsigned long)(p * (f->gbe_file_size >> 1)));

		for (ff = 0; ff < nff; ff++) {

			cat_buf(f->pad);
		}
	}
}

void
cat_buf(unsigned char *b)
{
	if (b == NULL)
		err(errno, "null pointer in cat command");

	if (rw_file_exact(STDOUT_FILENO, b,
	    GBE_PART_SIZE, 0, IO_WRITE, LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR) < 0)
		err(errno, "stdout: cat");
}

void
write_gbe_file(void)
{
	struct commands *cmd;
	struct xfile *f;

	struct stat _gbe_st;
	struct stat _tmp_st;

	unsigned long p;
	unsigned char update_checksum;

	check_null_command("write_gbe_file");

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
set_checksum(unsigned long p)
{
	check_null_command("set_checksum");

	check_bin(p, "part number");
	set_nvm_word(NVM_CHECKSUM_WORD, p, calculated_checksum(p));
}

unsigned short
calculated_checksum(unsigned long p)
{
	unsigned long c;
	unsigned int val16;

	check_null_command("calculated_checksum");

	val16 = 0;

	for (c = 0; c < NVM_CHECKSUM_WORD; c++)
		val16 += (unsigned int)nvm_word(c, p);

	return (unsigned short)((NVM_CHECKSUM - val16) & 0xffff);
}

/*
 * GbE NVM files store 16-bit (2-byte) little-endian words.
 * We must therefore swap the order when reading or writing.
 *
 * NOTE: The MAC address words are stored big-endian in the
 * file, but we assume otherwise and adapt accordingly.
 */

unsigned short
nvm_word(unsigned long pos16, unsigned long p)
{
	struct xfile *f;

	unsigned long pos;

	check_null_command("nvm_word");

	f = &x->f;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	return (unsigned short)f->buf[pos] |
	    ((unsigned short)f->buf[pos + 1] << 8);
}

void
set_nvm_word(unsigned long pos16, unsigned long p, unsigned short val16)
{
	struct xfile *f;

	unsigned long pos;

	check_null_command("set_nvm_word");

	f = &x->f;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	f->buf[pos] = (unsigned char)(val16 & 0xff);
	f->buf[pos + 1] = (unsigned char)(val16 >> 8);

	set_part_modified(p);
}

void
set_part_modified(unsigned long p)
{
	struct xfile *f;

	check_null_command("set_part_modified");

	f = &x->f;

	check_bin(p, "part number");
	f->part_modified[p] = 1;
}

void
check_nvm_bound(unsigned long c, unsigned long p)
{
	/*
	 * NVM_SIZE assumed as the limit, because this
	 * current design assumes that we will only
	 * ever modified the NVM area.
	 */

	check_null_command("check_nvm_bound");

	check_bin(p, "part number");

	if (c >= NVM_WORDS)
		err(ECANCELED, "check_nvm_bound: out of bounds %lu",
		    (unsigned long)c);
}

void
check_bin(unsigned long a, const char *a_name)
{
	if (a > 1)
		err(EINVAL, "%s must be 0 or 1, but is %lu",
		    a_name, (unsigned long)a);
}

void
rw_gbe_file_part(unsigned long p, int rw_type,
    const char *rw_type_str)
{
	struct commands *cmd;
	struct xfile *f;

	long rval;

	off_t file_offset;

	unsigned long gbe_rw_size;
	unsigned char *mem_offset;

	check_null_command("rw_gbe_file_part");

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
	struct commands *cmd;
	struct xfile *f;

	int saved_errno;
	int mv;

	check_null_command("write_to_gbe_bin");

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
	struct commands *cmd;
	struct xfile *f;

	long rval;

	unsigned long gbe_rw_size;

	off_t file_offset;
	unsigned char *mem_offset;

	struct stat st;
	unsigned char *buf_restore;

	check_null_command("check_written_part");

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
	struct xfile *f;

	unsigned long p;

	check_null_command("report_io_err_rw");

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
	struct xfile *f;

	int rval;

	int saved_errno;
	int tmp_gbe_bin_exists;

	char *dest_tmp;
	int dest_fd;

	check_null_command("gbe_mv");

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
 * Ensure rename() is durable by syncing the
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

	x_v_memcpy(dirbuf, path, pathlen + 1);
	slash = x_c_strrchr(dirbuf, '/');

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
	if (x_i_fsync(dirfd) == -1)
		goto err_fsync_dir;

	if (x_i_close(dirfd) == -1)
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
		x_i_close(dirfd);

	errno = saved_errno;

	return -1;
}

/*
 * This one is similar to gbe_file_offset,
 * but used to check Gbe bounds in memory,
 * and it is *also* used during file I/O.
 */
unsigned char *
gbe_mem_offset(unsigned long p, const char *f_op)
{
	struct xfile *f;

	off_t gbe_off;

	check_null_command("gbe_mem_offset");

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
	struct xfile *f;

	off_t gbe_file_half_size;

	check_null_command("gbe_file_offset");

	f = &x->f;

	gbe_file_half_size = f->gbe_file_size >> 1;

	return gbe_x_offset(p, f_op, "file",
	    gbe_file_half_size, f->gbe_file_size);
}

off_t
gbe_x_offset(unsigned long p, const char *f_op, const char *d_type,
    off_t nsize, off_t ncmp)
{
	struct xfile *f;

	off_t off;

	check_null_command("gbe_x_offset");

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
	struct xfile *f;

	long r;

	check_null_command("rw_gbe_file_exact");

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

void
check_null_command(const char *c)
{
	char msg[] = "undefined function name";
	char *func_name = msg;

	if (c != NULL) {

		if (*c != '\0') {

			func_name = (char *)c;
		}
	}

	if (x == NULL) {

		err(ECANCELED, "%s: x ptr is null", func_name);
	}
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

/*
 * rw_file_exact() - Read perfectly or die
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

/*
 * prw() - portable read-write
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
#ifndef MAX_EAGAIN_RETRIES
	unsigned long retries = 100000;
#else
	unsigned long retries = MAX_EAGAIN_RETRIES;
#endif

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
	if ((off_orig = lseek_loop(fd, (off_t)0, SEEK_CUR,
	    loop_eagain, loop_eintr)) == (off_t)-1) {
		r = -1;
	} else if (lseek_loop(fd, off, SEEK_SET,
	    loop_eagain, loop_eintr) == (off_t)-1) {
		r = -1;
	} else {
		verified = lseek_loop(fd, (off_t)0, SEEK_CUR,
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
			lseek_loop(fd, off, SEEK_SET,
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
			verified = lseek_loop(fd, (off_t)0, SEEK_CUR,
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
		     errno == try_err(loop_eagain, EAGAIN)) &&
		    retries++ < MAX_EAGAIN_RETRIES);
	}

	saved_errno = errno;

	off_last = lseek_loop(fd, off_orig, SEEK_SET,
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

/*
 * Check overflows caused by buggy libc.
 *
 * POSIX can say whatever it wants.
 * specification != implementation
 */
long
rw_over_nrw(long r, unsigned long nrw)
{
	/*
	 * If a byte length of zero
	 * was requested, that is
	 * clearly a bug. No way.
	 */
	if (!nrw)
		goto err_rw_over_nrw;

	if (r == -1)
		return r;

	if ((unsigned long)
	    r > X_LONG_MAX) {

		/*
		 * Theoretical buggy libc
		 * check. Extremely academic.
		 *
		 * Specifications never
		 * allow this return value
		 * to exceed SSIZE_T, but
		 * spec != implementation
		 *
		 * Check this after using
		 * [p]read() or [p]write()
		 */
		goto err_rw_over_nrw;
	}

	/*
	 * Theoretical buggy libc:
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
 * lseek_loop() does lseek() but optionally
 * on an EINTR/EAGAIN wait loop. Used by prw()
 * for setting offsets for positional I/O.
 */
off_t
lseek_loop(int fd, off_t off, int whence,
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

/*
 * If a given error loop is enabled,
 * e.g. EINTR or EAGAIN, an I/O operation
 * will loop until errno isn't -1 and one
 * of these, e.g. -1 and EINTR
 */
int
try_err(int loop_err, int errval)
{
	if (loop_err)
		return errval;

	/* errno is never negative,
	   so functions checking it
	   can use it accordingly */
	return -1;
}

void
usage(void)
{
	const char *util;

	util = getnvmprogname();

	fprintf(stderr,
	    "Modify Intel GbE NVM images e.g. set MAC\n"
	    "USAGE:\n"
	    "\t%s FILE dump\n"
	    "\t%s FILE setmac [MAC]\n"
	    "\t%s FILE swap\n"
	    "\t%s FILE copy 0|1\n"
	    "\t%s FILE cat\n"
	    "\t%s FILE cat16\n"
	    "\t%s FILE cat128\n",
	    util, util, util, util,
	    util, util, util);

	err(EINVAL, "Too few arguments");
}

void
err(int nvm_errval, const char *msg, ...)
{
	va_list args;

	if (errno == 0)
		errno = nvm_errval;
	if (!errno)
		errno = ECANCELED;

	(void)exit_cleanup();

	if (x != NULL)
		fprintf(stderr, "%s: ", getnvmprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	fprintf(stderr, ": %s\n", strerror(errno));

	exit(EXIT_FAILURE);
}

int
exit_cleanup(void)
{
	struct xfile *f;

	int close_err;
	int saved_errno;

	close_err = 0;
	saved_errno = errno;

	if (x != NULL) {
		f = &x->f;

		if (f->gbe_fd > -1) {
			if (x_i_close(f->gbe_fd) == -1)
				close_err = 1;
			f->gbe_fd = -1;
		}

		if (f->tmp_fd > -1) {
			if (x_i_close(f->tmp_fd) == -1)
				close_err = 1;
		}

		if (f->tname != NULL) {
			if (unlink(f->tname) == -1)
				close_err = 1;
		}

		f->tmp_fd = -1;
	}

	if (saved_errno)
		errno = saved_errno;

	if (close_err)
		return -1;

	return 0;
}

const char *
getnvmprogname(void)
{
	const char *p;
	static char fallback[] = "nvmutil";

	char *rval = fallback;

	if (x != NULL) {
		if (x->argv0 == NULL || *x->argv0 == '\0')
			return "";

		rval = x->argv0;
	}

	p = x_c_strrchr(rval, '/');

	if (p)
		return p + 1;
	else
		return rval;
}

/*
 * create new tmpfile path
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

	/*
	 * please do not modify the
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

		/*
		 * appended to filename for tmp:
		 */
		tmpdir_len = sizeof(default_tmpname);
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

	/*
	 * max length -1 of maxlen
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

		x_v_memcpy(dest + (unsigned long)1, tmpname, tmpname_len);

		x_v_memcpy(dest + (unsigned long)1 + tmpname_len,
		    default_tmpname, tmpdir_len);
	} else {

		x_v_memcpy(dest, base, tmpdir_len);

		dest[tmpdir_len] = '/';

		x_v_memcpy(dest + tmpdir_len + 1, tmpname, tmpname_len);
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
		x_i_close(fd_tmp);

	return NULL;
}

/*
 * portable mkstemp
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

char *
x_c_strrchr(const char *s, int c)
{
	const char *p = NULL;

	while (*s) {
		if (*s == (char)c)
			p = s;
		s++;
	}

	if (c == '\0')
		return (char *)s;

	return (char *)p;
}

/*
 * non-atomic rename
 *
 * commented because i can't sacrifice
 * exactly this property. nvmutil tries
 * to protect files against e.g. power loss
 */
/*
int
x_i_rename(const char *src, const char *dst)
{
	int sfd, dirfd;
	ssize_t r;
	char buf[8192];

	sfd = open(src, O_RDONLY);
	if (sfd < 0)
		return -1;

	dirfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (dirfd < 0) {
		x_i_close(sfd);
		return -1;
	}

	while ((r = read(sfd, buf, sizeof(buf))) > 0) {
		ssize_t w = write(dirfd, buf, r);
		if (w != r) {
			x_i_close(sfd);
			x_i_close(dirfd);
			return -1;
		}
	}

	if (r < 0) {
		x_i_close(sfd);
		x_i_close(dirfd);
		return -1;
	}

	x_i_fsync(dirfd);

	x_i_close(sfd);
	x_i_close(dirfd);

	if (unlink(src) < 0)
		return -1;

	return 0;
}
*/

char *
x_c_tmpdir(void)
{
	char *t;
	struct stat st;

	t = getenv("TMPDIR");
	if (t && *t) {
		if (stat(t, &st) == 0 && S_ISDIR(st.st_mode))
			return t;
	}

	if (stat("/tmp", &st) == 0 && S_ISDIR(st.st_mode))
		return "/tmp";

	if (stat("/var/tmp", &st) == 0 && S_ISDIR(st.st_mode))
		return "/var/tmp";

	return ".";
}

int
x_i_close(int fd)
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

void *
x_v_memcpy(void *dst, const void *src, unsigned long n)
{
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;

	while (n--)
		*d++ = *s++;

	return dst;
}

int
x_i_memcmp(const void *a, const void *b, unsigned long n)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;

	for ( ; n--; ++pa, ++pb)
		if (*pa != *pb)
			return *pa - *pb;

	return 0;
}

int
x_i_fsync(int fd)
{
	int r;

	do {
		r = fsync(fd);
	} while (r == -1 && errno == EINTR);

	return r;
}
