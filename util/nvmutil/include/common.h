/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 */

#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>

/* for linux getrandom
 */
#if defined(__linux__)
#include <errno.h>
#if defined(__has_include)
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#define HAVE_GETRANDOM 1
#endif
#endif
#if !defined(HAVE_GETRANDOM)
#include <sys/syscall.h>
#if defined(SYS_getrandom)
#define HAVE_GETRANDOM_SYSCALL 1
#endif
#endif

#endif

#define items(x) (sizeof((x)) / sizeof((x)[0]))

/* system prototypes
 */

int fchmod(int fd, mode_t mode);

/* analog of SSIZE_MAX
 */

#ifndef X_LONG_MAX
#define X_LONG_MAX ((long)(~((long)1 << (sizeof(long)*CHAR_BIT-1))))
#endif


/* build config
 */

#ifndef NVMUTIL_H
#define NVMUTIL_H

#define MAX_CMD_LEN 50

#ifndef PATH_LEN
#define PATH_LEN 4096
#endif

#define OFF_ERR 0
#ifndef OFF_RESET
#define OFF_RESET 1
#endif

#ifndef S_ISVTX
#define S_ISVTX 01000
#endif

#if defined(S_IFMT) && ((S_ISVTX & S_IFMT) != 0)
#error "Unexpected bit layout"
#endif

#ifndef MAX_ZERO_RW_RETRY
#define MAX_ZERO_RW_RETRY 5
#endif

#ifndef HAVE_REAL_PREAD_PWRITE
#define HAVE_REAL_PREAD_PWRITE 0
#endif

#ifndef LOOP_EAGAIN
#define LOOP_EAGAIN 1
#endif
#ifndef LOOP_EINTR
#define LOOP_EINTR 1
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

#define NO_LOOP_EAGAIN 0
#define NO_LOOP_EINTR 0

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

struct commands {
	unsigned long chk;
	char *str;
	void (*run)(void);
	int argc;
	unsigned char arg_part;
	unsigned char chksum_read;
	unsigned char chksum_write;
	unsigned long rw_size; /* within the 4KB GbE part */
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

	unsigned long part;
	unsigned char part_modified[2];
	unsigned char part_valid[2];

	unsigned char real_buf[GBE_BUF_SIZE];
	unsigned char bufcmp[GBE_BUF_SIZE]; /* compare gbe/tmp/reads */

	unsigned char pad[GBE_WORK_SIZE]; /* the file that wouldn't die */
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

	char *argv0;

	unsigned long i; /* index to cmd[] for current command */
	int no_cmd;

	/* Cat commands set this.
	   the cat cmd helpers check it */
	int cat;
};



struct xstate *xstatus(int argc, char *argv[]);

/* Sanitize command tables.
 */

void sanitize_command_list(void);
void sanitize_command_index(unsigned long c);

/* Argument handling (user input)
 */

void set_cmd(int argc, char *argv[]);
void set_cmd_args(int argc, char *argv[]);
unsigned long conv_argv_part_num(const char *part_str);
int xstrxcmp(const char *a, const char *b, unsigned long maxlen);

/* Prep files for reading
 */

void open_gbe_file(void);
int lock_file(int fd, int flags);
int same_file(int fd, struct stat *st_old, int check_size);
void xopen(int *fd, const char *path, int flags, struct stat *st);

/* Read GbE file and verify checksums
 */

void copy_gbe(void);
void read_file(void);
void read_checksums(void);
int good_checksum(unsigned long partnum);

/* validate commands
 */

void check_command_num(unsigned long c);
unsigned char valid_command(unsigned long c);

/* Helper functions for command: setmac
 */

void cmd_helper_setmac(void);
void parse_mac_string(void);
unsigned long xstrxlen(const char *scmp, unsigned long maxlen);
void set_mac_byte(unsigned long mac_byte_pos);
void set_mac_nib(unsigned long mac_str_pos,
    unsigned long mac_byte_pos, unsigned long mac_nib_pos);
unsigned short hextonum(char ch_s);
unsigned long rlong(void);
#if !(defined(FALLBACK_RAND_1989) && \
    ((FALLBACK_RAND_1989) > 0))
#if defined(__linux__)
#if defined(HAVE_GETRANDOM) || \
    defined(HAVE_GETRANDOM_SYSCALL)
int fallback_rand_getrandom(void *buf, size_t len);
#endif
#endif
#else
unsigned long fallback_rand_1989(void);
unsigned long entropy_jitter(void);
#endif
void write_mac_part(unsigned long partnum);

/* Helper functions for command: dump
 */

void cmd_helper_dump(void);
void print_mac_from_nvm(unsigned long partnum);
void hexdump(unsigned long partnum);

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
void cat(unsigned long nff);
void cat_buf(unsigned char *b);

/* Command verification/control
 */

void check_cmd(void (*fn)(void), const char *name);
void cmd_helper_err(void);

/* Write GbE files to disk
 */

void write_gbe_file(void);
void set_checksum(unsigned long part);
unsigned short calculated_checksum(unsigned long p);

/* NVM read/write
 */

unsigned short nvm_word(unsigned long pos16, unsigned long part);
void set_nvm_word(unsigned long pos16,
    unsigned long part, unsigned short val16);
void set_part_modified(unsigned long p);
void check_nvm_bound(unsigned long pos16, unsigned long part);
void check_bin(unsigned long a, const char *a_name);

/* GbE file read/write
 */

void rw_gbe_file_part(unsigned long p, int rw_type,
    const char *rw_type_str);
void write_to_gbe_bin(void);
int gbe_mv(void);
void check_written_part(unsigned long p);
void report_io_err_rw(void);
unsigned char *gbe_mem_offset(unsigned long part, const char *f_op);
off_t gbe_file_offset(unsigned long part, const char *f_op);
off_t gbe_x_offset(unsigned long part, const char *f_op,
    const char *d_type, off_t nsize, off_t ncmp);
long rw_gbe_file_exact(int fd, unsigned char *mem, unsigned long nrw,
    off_t off, int rw_type);

/* Generic read/write
 */

int fsync_dir(const char *path);
long rw_file_exact(int fd, unsigned char *mem, unsigned long len,
    off_t off, int rw_type, int loop_eagain, int loop_eintr,
    unsigned long max_retries, int off_reset);
long prw(int fd, void *mem, unsigned long nrw,
    off_t off, int rw_type, int loop_eagain, int loop_eintr,
    int off_reset);
int io_args(int fd, void *mem, unsigned long nrw,
    off_t off, int rw_type);
int check_file(int fd, struct stat *st);
long rw_over_nrw(long r, unsigned long nrw);
#if !defined(HAVE_REAL_PREAD_PWRITE) || \
    HAVE_REAL_PREAD_PWRITE < 1
off_t lseek_on_eintr(int fd, off_t off,
    int whence, int loop_eagain, int loop_eintr);
#endif
int try_err(int loop_err, int errval);

/* Error handling and cleanup
 */

void usage(void);
void err(int nvm_errval, const char *msg, ...);
int exit_cleanup(void);
const char *getnvmprogname(void);

/* Portable libc functions
 */

char *new_tmpfile(int *fd, int local, const char *path);
int mkstemp_n(char *template);
char *get_tmpdir(void);
int close_on_eintr(int fd);
int fsync_on_eintr(int fd);

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
typedef char static_assert_unsigned_long_is_4[
    (sizeof(unsigned long) >= 4) ? 1 : -1];
typedef char static_assert_long_ulong[
    (sizeof(unsigned long) == sizeof(long)) ? 1 : -1];
typedef char static_assert_int_ge_32[(sizeof(int) >= 4) ? 1 : -1];
typedef char static_assert_twos_complement[
    ((-1 & 3) == 3) ? 1 : -1
];
typedef char assert_unsigned_long_ptr[
    (sizeof(unsigned long) >= sizeof(void *)) ? 1 : -1
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
typedef char assert_pathlen[(PATH_LEN>=256)?1:-1];
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
typedef char bool_loop_eintr[(LOOP_EINTR==1||LOOP_EINTR==0)?1:-1];
typedef char bool_loop_eagain[(LOOP_EAGAIN==1||LOOP_EAGAIN==0)?1:-1];
typedef char bool_no_loop_eintr[(NO_LOOP_EINTR==0)?1:-1];
typedef char bool_no_loop_eagain[(NO_LOOP_EAGAIN==0)?1:-1];
typedef char bool_off_err[(OFF_ERR==0)?1:-1];
typedef char bool_off_reset[(OFF_RESET==0||OFF_RESET==1)?1:-1];

#endif
#endif
