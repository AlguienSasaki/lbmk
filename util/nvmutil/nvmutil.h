/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 */

#ifndef NVMUTIL_H
#define NVMUTIL_H

#define MAX_CMD_LEN 50

#ifndef PATH_LEN
#define PATH_LEN 1024
#endif

#define OFF_ERR 0
#ifndef OFF_RESET
#define OFF_RESET 1
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

/*
 * Older versions of BSD to the early 2000s
 * could compile nvmutil, but pledge was
 * added in the 2010s. Therefore, for extra
 * portability, we will only pledge/unveil
 * on OpenBSD versions that have it.
 */

#if defined(__OpenBSD__) && defined(OpenBSD)
#if OpenBSD >= 604
#ifndef NVMUTIL_UNVEIL
#define NVMUTIL_UNVEIL 1
#endif
#endif
#if OpenBSD >= 509
#ifndef NVMUTIL_PLEDGE
#define NVMUTIL_PLEDGE 1
#endif
#endif
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
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

/*
 * Sizes in bytes:
 */

#define SIZE_1KB 1024
#define SIZE_4KB (4 * SIZE_1KB)
#define SIZE_8KB (8 * SIZE_1KB)
#define SIZE_16KB (16 * SIZE_1KB)
#define SIZE_128KB (128 * SIZE_1KB)

#define GBE_BUF_SIZE (SIZE_128KB)

/*
 * First 128 bytes of a GbE part contains
 * the regular NVM (Non-Volatile-Memory)
 * area. All of these bytes must add up,
 * truncated to 0xBABA.
 *
 * The full GbE region is 4KB, but only
 * the first 128 bytes are used here.
 *
 * There is a second 4KB part with the same
 * rules, and it *should* be identical.
 */

#define GBE_WORK_SIZE (SIZE_8KB)
#define GBE_PART_SIZE (GBE_WORK_SIZE >> 1)
#define NVM_CHECKSUM 0xBABA
#define NVM_SIZE 128
#define NVM_WORDS (NVM_SIZE >> 1)
#define NVM_CHECKSUM_WORD (NVM_WORDS - 1)

/*
 * Portable macro based on BSD nitems.
 * Used to count the number of commands (see below).
 */

#define items(x) (sizeof((x)) / sizeof((x)[0]))

/*
 * GbE files can be 8KB, 16KB or 128KB,
 * but we only need the two 4KB parts
 * from offset zero and offset 64KB in
 * a 128KB file, or zero and 8KB in a 16KB
 * file, or zero and 4KB in an 8KB file.
 *
 * The code will handle this properly.
 */

#ifndef X_LONG_MAX
#define X_LONG_MAX ((long)(~((long)1 << (sizeof(long)*CHAR_BIT-1))))
#endif

/*
 * Use these for .argc in command[]:
 */

#define ARGC_3 3
#define ARGC_4 4

#define NO_LOOP_EAGAIN 0
#define NO_LOOP_EINTR 0

/*
 * Used for checking whether.
 * a file is a file via stat().
 *
 * Portable macro for compatibility
 * with older unix e.g. v7 unix (has S_IFREG),
 * 4.2bsd (has S_IFMT) or POSIX (has S_ISREG)
 *
 * Fallback works where S_IFREG == 0100000
 * (classic unix bitmask)
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

/*
 * Used as indices for command[]
 * MUST be in the same order as entries in command[]
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

/*
 * portable timeval
 */
struct x_st_timeval {
	long tv_sec;
	long tv_usec;
};

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

struct macaddr {
	char *str; /* set to rmac, or argv string */
	char rmac[18]; /* xx:xx:xx:xx:xx:xx */
	unsigned short mac_buf[3];
};

struct xfile {
	int gbe_fd;
	int tmp_fd;

	char *tname; /* path of tmp file */
	char *fname; /* path of gbe file */

	unsigned char *buf; /* work memory for files */

	int io_err_gbe; /* intermediary write (verification) */
	int io_err_gbe_bin; /* final write (real file) */
	int rw_check_err_read[2];
	int rw_check_partial_read[2];
	int rw_check_bad_part[2];

	int post_rw_checksum[2];

	dev_t gbe_dev;
	ino_t gbe_ino;

	dev_t tmp_dev;
	ino_t tmp_ino;

	off_t gbe_file_size;
	off_t gbe_tmp_size;

	unsigned long part;
	unsigned char part_modified[2];
	unsigned char part_valid[2];

	unsigned char real_buf[GBE_BUF_SIZE];
	unsigned char bufcmp[GBE_BUF_SIZE]; /* compare gbe/tmp/reads */

	unsigned char pad[GBE_WORK_SIZE]; /* the file that wouldn't die */
};

/*
 * BE CAREFUL when editing this
 * to ensure that you also update
 * the tables in new_xstate()
 */
struct xstate {
	struct commands cmd[7];
	struct macaddr mac;
	struct xfile f;

	char *argv0;

	unsigned long i; /* index to cmd[] for current command */
	int no_cmd;

	/* store size of a struct here.
	   (can be used to copy old state) */
	unsigned long xsize;
};




static struct xstate *new_xstate(void);

/*
 * Sanitize command tables.
 */
void sanitize_command_list(void);
void sanitize_command_index(unsigned long c);

/*
 * Argument handling (user input)
 */
void set_cmd(int argc, char *argv[]);
void set_cmd_args(int argc, char *argv[]);
unsigned long conv_argv_part_num(const char *part_str);
int xstrxcmp(const char *a, const char *b, unsigned long maxlen);

/*
 * Prep files for reading
 */
void open_gbe_file(void);
int lock_file(int fd);
void xopen(int *fd, const char *path, int flags, struct stat *st);

/*
 * Read GbE file and verify
 * checksums.
 *
 * After this, we can run commands.
 */
void copy_gbe(void);
void read_checksums(void);
int good_checksum(unsigned long partnum);

/*
 * Execute user command on GbE data.
 * These are stubs that call helpers.
 */
void run_cmd(void);
void check_command_num(unsigned long c);
unsigned char valid_command(unsigned long c);

/*
 * Helper functions for command: setmac
 */
void cmd_helper_setmac(void);
void parse_mac_string(void);
unsigned long xstrxlen(const char *scmp, unsigned long maxlen);
void set_mac_byte(unsigned long mac_byte_pos);
void set_mac_nib(unsigned long mac_str_pos,
    unsigned long mac_byte_pos, unsigned long mac_nib_pos);
unsigned short hextonum(char ch_s);
unsigned long rlong(void);
unsigned long entropy_jitter(void);
int x_i_gettimeofday(struct x_st_timeval *tv, void *tz);
void write_mac_part(unsigned long partnum);

/*
 * Helper functions for command: dump
 */
void cmd_helper_dump(void);
void print_mac_from_nvm(unsigned long partnum);
void hexdump(unsigned long partnum);

/*
 * Helper functions for command: swap
 */
void cmd_helper_swap(void);

/*
 * Helper functions for command: copy
 */
void cmd_helper_copy(void);

/*
 * Helper functions for commands:
 * cat, cat16 and cat128
 */
void cmd_helper_cat(void);
void cat_buf(unsigned char *b);

/*
 * After command processing, write
 * the modified GbE file back.
 *
 * These are stub functions: check
 * below for the actual functions.
 */
void write_gbe_file(void);
void set_checksum(unsigned long part);
unsigned short calculated_checksum(unsigned long p);

/*
 * Helper functions for accessing
 * the NVM area during operation.
 */
unsigned short nvm_word(unsigned long pos16, unsigned long part);
void set_nvm_word(unsigned long pos16,
    unsigned long part, unsigned short val16);
void set_part_modified(unsigned long p);
void check_nvm_bound(unsigned long pos16, unsigned long part);
void check_bin(unsigned long a, const char *a_name);

/*
 * Helper functions for stub functions
 * that handle GbE file reads/writes.
 */
void rw_gbe_file_part(unsigned long p, int rw_type,
    const char *rw_type_str);
void write_to_gbe_bin(void);
int gbe_mv(void);
void check_written_part(unsigned long p);
void report_io_err_rw(void);
int fsync_dir(const char *path);
unsigned char *gbe_mem_offset(unsigned long part, const char *f_op);
off_t gbe_file_offset(unsigned long part, const char *f_op);
off_t gbe_x_offset(unsigned long part, const char *f_op,
    const char *d_type, off_t nsize, off_t ncmp);
long rw_gbe_file_exact(int fd, unsigned char *mem, unsigned long nrw,
    off_t off, int rw_type);
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
off_t lseek_loop(int fd, off_t off,
    int whence, int loop_eagain, int loop_eintr);
#endif
int try_err(int loop_err, int errval);

/*
 * Error handling and cleanup
 */
void usage(void);
void err(int nvm_errval, const char *msg, ...);
int exit_cleanup(void);
const char *getnvmprogname(void);

/*
 * a special kind of hell
 */
char *new_tmpfile(int *fd, int local, const char *path);
int x_i_mkstemp(char *template);
char *x_c_strrchr(const char *s, int c);
/* x_i_rename not suitable
 * for atomic writes. kept
 * commentted for use in a
 * library in the future */
/*
int x_i_rename(const char *src, const char *dst);
*/
char *x_c_tmpdir(void);
int x_i_close(int fd);
void *x_v_memcpy(void *dst,
    const void *src, unsigned long n);
int x_i_memcmp(const void *a,
    const void *b, unsigned long n);
int x_i_fchmod(int fd, mode_t mode);
int x_try_fdpath(const char *prefix,
    int fd, mode_t mode);
unsigned long x_conv_fd(char *buf,
    unsigned long n);
int x_i_fsync(int fd);

#endif
