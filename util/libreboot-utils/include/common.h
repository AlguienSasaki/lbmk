/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>

 TODO: this file should be split, into headers for each
       C source file specifically. it was originally just
       for nvmutil, until i added mkhtemp to the mix
 */


#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>

/* dangerously cool macros:
 */

#define SUCCESS(x) ((x) >= 0)

/* syscalls can set errno even on success; this
 * is rare, but permitted. in various functions, we
 * reset errno on success, to what the caller had,
 * but we must still honour what was returned.
 *
 * lib/file.c is littered with examples
 */
#define reset_caller_errno(return_value) \
	do { \
		if (SUCCESS(return_value) && (!errno)) \
			errno = saved_errno; \
	} while (0)

#define items(x) (sizeof((x)) / sizeof((x)[0]))

#define MKHTEMP_RETRY_MAX 512
#define MKHTEMP_SPIN_THRESHOLD 32

#define MKHTEMP_FILE 0
#define MKHTEMP_DIR  1


/* if 1: on operations that
 * check ownership, always
 * permit root to access even
 * if not the file/dir owner
 */
#ifndef ALLOW_ROOT_OVERRIDE
#define ALLOW_ROOT_OVERRIDE 0
#endif

/*
 */

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(~((ssize_t)1 << (sizeof(ssize_t)*CHAR_BIT-1))))
#endif


/* build config
 */

#ifndef NVMUTIL_H
#define NVMUTIL_H

#define MAX_CMD_LEN 50

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef PATH_MAX
#error PATH_MAX_undefined
#elif ((PATH_MAX) < 1024)
#error PATH_MAX_too_low
#endif

#ifndef S_ISVTX
#define S_ISVTX 01000
#endif

#if defined(S_IFMT) && ((S_ISVTX & S_IFMT) != 0)
#error "Unexpected bit layout"
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_EXCL
#define O_EXCL 0
#endif

#ifndef O_CREAT
#define O_CREAT 0
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#ifndef FD_CLOEXEC
#define FD_CLOEXEC 0
#endif

/* Sizes in bytes:
 */

#define SIZE_1KB 1024
#define SIZE_4KB (4 * SIZE_1KB)
#define SIZE_8KB (8 * SIZE_1KB)
#define SIZE_16KB (16 * SIZE_1KB)
#define SIZE_128KB (128 * SIZE_1KB)

#define GBE_BUF_SIZE (SIZE_128KB)

/* First 128 bytes of gbe.bin is NVM.
 * Then extended area. All of NVM must
 * add up to BABA, truncated (LE)
 *
 * First 4KB of each half of the file
 * contains NVM+extended.
 */

#define GBE_WORK_SIZE (SIZE_8KB)
#define GBE_PART_SIZE (GBE_WORK_SIZE >> 1)
#define NVM_CHECKSUM 0xBABA
#define NVM_SIZE 128
#define NVM_WORDS (NVM_SIZE >> 1)
#define NVM_CHECKSUM_WORD (NVM_WORDS - 1)

/* argc minimum (dispatch)
 */

#define ARGC_3 3
#define ARGC_4 4

/* For checking if an fd is a normal file.
 * Portable for old Unix e.g. v7 (S_IFREG),
 * 4.2BSD (S_IFMT), POSIX (S_ISREG).
 *
 * IFREG: assumed 0100000 (classic bitmask)
 */

#ifndef S_ISREG
#if defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#elif defined(S_IFREG)
#define S_ISREG(m) (((m) & S_IFREG) != 0)
#else
#error "can't determine types with stat()"
#endif
#endif

#define IO_READ 0
#define IO_WRITE 1
#define IO_PREAD 2
#define IO_PWRITE 3

/* for nvmutil commands
 */

#define CMD_DUMP 0
#define CMD_SETMAC 1
#define CMD_SWAP 2
#define CMD_COPY 3
#define CMD_CAT 4
#define CMD_CAT16 5
#define CMD_CAT128 6

#define ARG_NOPART 0
#define ARG_PART 1

#define SKIP_CHECKSUM_READ 0
#define CHECKSUM_READ 1

#define SKIP_CHECKSUM_WRITE 0
#define CHECKSUM_WRITE 1

/* command table
 */

typedef void (*func_t)(void);

struct commands {
	size_t chk;
	char *str;
	func_t run;
	int argc;
	unsigned char arg_part;
	unsigned char chksum_read;
	unsigned char chksum_write;
	size_t rw_size; /* within the 4KB GbE part */
	int flags; /* e.g. O_RDWR or O_RDONLY */
};

/* mac address
 */

struct macaddr {
	char *str; /* set to rmac, or argv string */
	char rmac[18]; /* xx:xx:xx:xx:xx:xx */
	unsigned short mac_buf[3];
};

/* gbe.bin and tmpfile
 */

struct xfile {
	int gbe_fd;
	struct stat gbe_st;

	int tmp_fd;
	struct stat tmp_st;

	char *tname; /* path of tmp file */
	char *fname; /* path of gbe file */

	unsigned char *buf; /* work memory for files */

	int io_err_gbe; /* intermediary write (verification) */
	int io_err_gbe_bin; /* final write (real file) */
	int rw_check_err_read[2];
	int rw_check_partial_read[2];
	int rw_check_bad_part[2];

	int post_rw_checksum[2];

	off_t gbe_file_size;
	off_t gbe_tmp_size;

	size_t part;
	unsigned char part_modified[2];
	unsigned char part_valid[2];

	unsigned char real_buf[GBE_BUF_SIZE];
	unsigned char bufcmp[GBE_BUF_SIZE]; /* compare gbe/tmp/reads */

	unsigned char pad[GBE_WORK_SIZE]; /* the file that wouldn't die */

	/* we later rename in-place, using old fd. renameat() */
	int dirfd;
	char *base;
	char *tmpbase;
};

/* Command table, MAC address, files
 *
 * BE CAREFUL when editing this
 * to ensure that you also update
 * the tables in xstatus()
 */

struct xstate {
	struct commands cmd[7];
	struct macaddr mac;
	struct xfile f;

	size_t i; /* index to cmd[] for current command */
	int no_cmd;

	/* Cat commands set this.
	   the cat cmd helpers check it */
	int cat;
};

struct filesystem {
	int rootfd;
};

struct xstate *xstart(int argc, char *argv[]);
struct xstate *xstatus(void);

/* Sanitize command tables.
 */

void sanitize_command_list(void);
void sanitize_command_index(size_t c);

/* Argument handling (user input)
 */

void set_cmd(int argc, char *argv[]);
void set_cmd_args(int argc, char *argv[]);
size_t conv_argv_part_num(const char *part_str);

/* Prep files for reading
 */

void open_gbe_file(void);
int fd_verify_regular(int fd,
    const struct stat *expected,
    struct stat *out);
int fd_verify_identity(int fd,
    const struct stat *expected,
    struct stat *out);
int fd_verify_dir_identity(int fd,
    const struct stat *expected);
int is_owner(struct stat *st);
int lock_file(int fd, int flags);
int same_file(int fd, struct stat *st_old, int check_size);

/* Read GbE file and verify checksums
 */

void copy_gbe(void);
void read_file(void);
void read_checksums(void);
int good_checksum(size_t partnum);

/* validate commands
 */

void check_command_num(size_t c);
unsigned char valid_command(size_t c);

/* Helper functions for command: setmac
 */

void cmd_helper_setmac(void);
void parse_mac_string(void);
void set_mac_byte(size_t mac_byte_pos);
void set_mac_nib(size_t mac_str_pos,
    size_t mac_byte_pos, size_t mac_nib_pos);
void write_mac_part(size_t partnum);

/* string functions
 */

size_t page_remain(const void *p);
long pagesize(void);
int xunveilx(const char *path, const char *permissions);
int xpledgex(const char *promises, const char *execpromises);
char *smalloc(char **buf, size_t size);
void *vmalloc(void **buf, size_t size);
size_t slen(const char *scmp, size_t maxlen,
    size_t *rval);
int vcmp(const void *s1, const void *s2, size_t n);
int scmp(const char *a, const char *b,
    size_t maxlen, int *rval);
int ccmp(const char *a, const char *b, size_t i,
    int *rval);
char *sdup(const char *s,
    size_t n, char **dest);
char *scatn(ssize_t sc, const char **sv,
    size_t max, char **rval);
char *scat(const char *s1, const char *s2,
    size_t n, char **dest);
void dcat(const char *s, size_t n,
    size_t off, char **dest1,
    char **dest2);
/* numerical functions
 */

unsigned short hextonum(char ch_s);
void spew_hex(const void *data, size_t len);
void *rmalloc(size_t n);
void rset(void *buf, size_t n);
void *rmalloc(size_t n);
char *rchars(size_t n);
size_t rsize(size_t n);

/* Helper functions for command: dump
 */

void cmd_helper_dump(void);
void print_mac_from_nvm(size_t partnum);

/* Helper functions for command: swap
 */

void cmd_helper_swap(void);

/* Helper functions for command: copy
 */

void cmd_helper_copy(void);

/* Helper functions for commands:
 * cat, cat16 and cat128
 */

void cmd_helper_cat(void);
void cmd_helper_cat16(void);
void cmd_helper_cat128(void);
void cat(size_t nff);
void cat_buf(unsigned char *b);

/* Command verification/control
 */

void check_cmd(void (*fn)(void), const char *name);
void cmd_helper_err(void);

/* Write GbE files to disk
 */

void write_gbe_file(void);
void set_checksum(size_t part);
unsigned short calculated_checksum(size_t p);

/* NVM read/write
 */

unsigned short nvm_word(size_t pos16, size_t part);
void set_nvm_word(size_t pos16,
    size_t part, unsigned short val16);
void set_part_modified(size_t p);
void check_nvm_bound(size_t pos16, size_t part);
void check_bin(size_t a, const char *a_name);

/* GbE file read/write
 */

void rw_gbe_file_part(size_t p, int rw_type,
    const char *rw_type_str);
void write_to_gbe_bin(void);
int gbe_mv(void);
void check_written_part(size_t p);
void report_io_err_rw(void);
unsigned char *gbe_mem_offset(size_t part, const char *f_op);
off_t gbe_file_offset(size_t part, const char *f_op);
off_t gbe_x_offset(size_t part, const char *f_op,
    const char *d_type, off_t nsize, off_t ncmp);
ssize_t rw_gbe_file_exact(int fd, unsigned char *mem, size_t nrw,
    off_t off, int rw_type);

/* Generic read/write
 */

int fsync_dir(const char *path);
ssize_t rw_exact(int fd, unsigned char *mem, size_t len,
    off_t off, int rw_type);
ssize_t rw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type);
int io_args(int fd, void *mem, size_t nrw,
    off_t off, int rw_type);
int check_file(int fd, struct stat *st);
ssize_t rw_over_nrw(ssize_t r, size_t nrw);
int sys_retry(int saved_errno, long rval);
int fs_retry(int saved_errno, int rval);
int rw_retry(int saved_errno, ssize_t rval);

/* Error handling and cleanup
 */

void usage(void);
int with_fallback_errno(int fallback);
void exitf(const char *msg, ...);
func_t errhook(func_t ptr); /* hook function for cleanup on err */
const char *lbgetprogname(void);
void no_op(void);
void err_mkhtemp(int errval, const char *msg, ...);

/* libc hardening
 */

int new_tmpfile(int *fd, char **path, char *tmpdir,
    const char *template);
int new_tmpdir(int *fd, char **path, char *tmpdir,
    const char *template);
int new_tmp_common(int *fd, char **path, int type,
    char *tmpdir, const char *template);
int mkhtemp_try_create(int dirfd,
    struct stat *st_dir_first,
    char *fname_copy,
    char *p,
    size_t xc,
    int *fd,
    struct stat *st,
    int type);
int
mkhtemp_tmpfile_linux(int dirfd,
    struct stat *st_dir_first,
    char *fname_copy,
    char *p,
    size_t xc,
    int *fd,
    struct stat *st);
int mkhtemp(int *fd, struct stat *st,
    char *template, int dirfd, const char *fname,
    struct stat *st_dir_first, int type);
int world_writeable_and_sticky(const char *s,
    int sticky_allowed, int always_sticky);
int same_dir(const char *a, const char *b);
int tmpdir_policy(const char *path,
    int *allow_noworld_unsticky);
char *env_tmpdir(int always_sticky, char **tmpdir,
    char *override_tmpdir);
int secure_file(int *fd,
    struct stat *st,
    struct stat *expected,
    int bad_flags,
    int check_seek,
    int do_lock,
    mode_t mode);
void xclose(int *fd);
int fsync_on_eintr(int fd);
int fs_rename_at(int olddirfd, const char *old,
             int newdirfd, const char *new);
int fs_open(const char *path, int flags);
void free_and_set_null(char **buf);
void open_file_on_eintr(const char *path, int *fd, int flags, mode_t mode,
    struct stat *st);
struct filesystem *rootfs(void);
int fs_resolve_at(int dirfd, const char *path, int flags);
int fs_next_component(const char **p,
    char *name, size_t namesz);
int fs_open_component(int dirfd, const char *name,
    int flags, int is_last);
int fs_dirname_basename(const char *path,
    char **dir, char **base, int allow_relative);
int openat_on_eintr(int dirfd, const char *path,
    int flags, mode_t mode);
int mkdirat_on_eintr(int dirfd, 
    const char *pathname, mode_t mode);
int if_err(int condition, int errval);
int if_err_sys(int condition);
char *lbsetprogname(char *argv0);

/* asserts */

/* type asserts */
typedef char static_assert_char_is_8_bits[(CHAR_BIT == 8) ? 1 : -1];
typedef char static_assert_char_is_1[(sizeof(char) == 1) ? 1 : -1];
typedef char static_assert_unsigned_char_is_1[
    (sizeof(unsigned char) == 1) ? 1 : -1];
typedef char static_assert_unsigned_short_is_2[
    (sizeof(unsigned short) >= 2) ? 1 : -1];
typedef char static_assert_short_is_2[(sizeof(short) >= 2) ? 1 : -1];
typedef char static_assert_unsigned_int_is_4[
    (sizeof(unsigned int) >= 4) ? 1 : -1];
typedef char static_assert_unsigned_ssize_t_is_4[
    (sizeof(size_t) >= 4) ? 1 : -1];
typedef char static_assert_ssize_t_ussize_t[
    (sizeof(size_t) == sizeof(ssize_t)) ? 1 : -1];
typedef char static_assert_int_ge_32[(sizeof(int) >= 4) ? 1 : -1];
typedef char static_assert_twos_complement[
    ((-1 & 3) == 3) ? 1 : -1
];
typedef char assert_unsigned_ssize_t_ptr[
    (sizeof(size_t) >= sizeof(void *)) ? 1 : -1
];

/*
 * We set _FILE_OFFSET_BITS 64, but we only handle
 * but we only need smaller files, so require 4-bytes.
 * Some operating systems ignore the define, hence assert:
 */
typedef char static_assert_off_t_is_32[(sizeof(off_t) >= 4) ? 1 : -1];

/*
 * asserts (variables/defines sanity check)
 */
typedef char assert_argc3[(ARGC_3==3)?1:-1];
typedef char assert_argc4[(ARGC_4==4)?1:-1];
typedef char assert_read[(IO_READ==0)?1:-1];
typedef char assert_write[(IO_WRITE==1)?1:-1];
typedef char assert_pread[(IO_PREAD==2)?1:-1];
typedef char assert_pwrite[(IO_PWRITE==3)?1:-1];
typedef char assert_pathlen[(PATH_MAX>=1024)?1:-1];
/* commands */
typedef char assert_cmd_dump[(CMD_DUMP==0)?1:-1];
typedef char assert_cmd_setmac[(CMD_SETMAC==1)?1:-1];
typedef char assert_cmd_swap[(CMD_SWAP==2)?1:-1];
typedef char assert_cmd_copy[(CMD_COPY==3)?1:-1];
typedef char assert_cmd_cat[(CMD_CAT==4)?1:-1];
typedef char assert_cmd_cat16[(CMD_CAT16==5)?1:-1];
typedef char assert_cmd_cat128[(CMD_CAT128==6)?1:-1];
/* bool */
typedef char bool_arg_nopart[(ARG_NOPART==0)?1:-1];
typedef char bool_arg_part[(ARG_PART==1)?1:-1];
typedef char bool_skip_checksum_read[(SKIP_CHECKSUM_READ==0)?1:-1];
typedef char bool_checksum_read[(CHECKSUM_READ==1)?1:-1];
typedef char bool_skip_checksum_write[(SKIP_CHECKSUM_WRITE==0)?1:-1];
typedef char bool_checksum_write[(CHECKSUM_WRITE==1)?1:-1];

#endif
#endif
