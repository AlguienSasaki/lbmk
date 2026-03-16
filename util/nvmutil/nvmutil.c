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

/*
 * In practise, most people
 * aren't going to use very
 * long names, so even on old
 * systems with weaker limits,
 * it's OK to set this higher.
 *
 * 4096 is a good, conservative
 * default these days.
 */
#ifndef PATH_LEN
#ifdef PATH_MAX
#define PATH_LEN (PATH_MAX)
#else
#define PATH_LEN 4096
#endif
#endif

#define OFF_ERR 0
#ifndef OFF_RESET
#define OFF_RESET 1
#endif

/*
 * I/O config (build-time)
 *
 * Regarding:
 * Retries on zero-return.
 *
 * 5 retries is generous,
 * but also conservative.
 * This is enough for e.g.
 * slow USB flash drives,
 * busy NFS servers, etc.
 * Any more is too much
 * and not of much benefit.
 *
 * 3-5 will tolerate buggy
 * USB drives for example,
 * but won't spin as long
 * on really buggy and slow
 * networks e.g. slow NFS.
 *
 * At least 3-5 recommended.
 * Pass this at build time.
 */
#ifndef MAX_ZERO_RW_RETRY
#define MAX_ZERO_RW_RETRY 5
#endif
/*
 * 0: portable pread/pwrite
 * 1: real pread/pwrite (thread-safe)
 * Pass this at build-time
 */
#ifndef HAVE_REAL_PREAD_PWRITE
#define HAVE_REAL_PREAD_PWRITE 0
#endif
/*
 * Configure whether to wait on
 * EINTR on files, or EAGAIN on
 * cmd cat (stdout).
 *
 * Pass these at build time.
 */
#ifndef LOOP_EAGAIN
#define LOOP_EAGAIN 1
#endif
#ifndef LOOP_EINTR
#define LOOP_EINTR 1
#endif

/*
 * Major TODO: split this into multiple files.
 * This program has become quite large now, mostly
 * due to all the extra sanity checks / portability.
 * Make most of nvmutil a *library* for re-use
 *
 * TODO: gettimeofday not posible - use portable functions.
 * TODO: ux fallback: modify the program instead
 * to run on 16-bit systems: smaller buffers, and do
 * operations byte-based instead of word-based.
 *
 * TODO: _XOPEN_SOURCE 500 probably not needed anymore.
 * the portable fallbacks alone are likely enough.
 * e.g. i don't need stdint, and i don't use pwrite/pread
 * anymore.
 *
 * TODO: version detection of various BSDs to detect
 * arc4random, use that if available. but also work on
 * older versions of those BSDs (also MacOS) that lack it.
 *
 * TODO: portability/testing on non-Unix systems:
 * old DOS. all windows versions (probably irrelevant
 * because you can use cygwin/wsl, whatever), classic MacOS,
 * also test really old unix e.g. sunos and irix. Be/Haiku too!
 *
 * TODO: reliance on global variables for status. make
 * functions use structs passed as args instead, make
 * functions re-useable (including libraries), etc.
 *
 * TODO: bound checks for files per-command, e.g. only
 * first 6 bytes for CMD_SETMAC
 *
 * TODO: in command sanitizer: verify that each given
 * entry corresponds to the correct function, in the
 * pointer (this check is currently missing)
 *
 * TODO: general modularisierung of the entire codebase.
 * TODO: better explain copy/swap read inversion trick
 *       by improving existing comments
 * TODO: lots of overwritten comments in code. tidy it up.
 *
 * TODO: use getopt for nvmutil args, so that multiple
 *      operations can be performed, and also on many
 *	files at once (noting limitations with cat)
 *	BONUS: implement own getopt(), for portability
 *
 * TODO: document fuzzing / static analysis methods
 * 	for the code, and:
 * TODO: implement rigorous unit tests (separate util)
 *	NOTE: this would *include* known good test files
 *	in various configurations, also invalid files.
 *	the tests would likely be portable posix shell
 *	scripts rather than a new C program, but a modularisiert
 *	codebase would allow me to write a separate C
 *	program to test some finer intricacies
 * TODO: the unit tests would basically test regressions
 * TODO: after writing back a gbe to file, close() and
 *	open() it again, read it again, and check that
 *	the contents were written correctly, providing
 *	a warning if they were. do this in the main
 *	program.
 * TODO: the unit tests would include an aggressive set
 *	of fuzz tests, under controlled conditions
 *
 * TODO: also document the layout of Intel GbE files, so
 *       that wily individuals can easily expand the
 *	featureset of nvmutil.
 * TODO: write a manpage
 * TODO: simplify the command sanitization, implement more
 *	of it as build time checks, e.g. static asserts.
 *	generally remove cleverness from the code, instead
 *	prefyerring readibility
 * TODO: also document nvmutil's coding style, which is
 *	its own style at this point!
 * TODO: when all the above (and possibly more) is done,
 *	submit this tool to coreboot with a further change
 *	to their build system that lets users modify
 *	GbE images, especially set MAC addresses, when
 *	including GbE files in coreboot configs.
 */
/*
 BONUS TODO:
 CI/CD. woodpecker is good enough, sourcehut also has one.
	tie this in with other things mentioned here, 
	e.g. fuzzer / unit tests
*/

/* Major TODO: reproducible builds
Test with and without these:

CFLAGS += -fno-record-gcc-switches
CFLAGS += -ffile-prefix-map=$(PWD)=.
CFLAGS += -fdebug-prefix-map=$(PWD)=.

I already avoid unique timestamps per-build,
by not using them, e.g. not reporting build
time in the program.

When splitting the nvmutil.c file later, do e.g.:

SRC = main.c io.c nvm.c cmd.c
OBJ = $(SRC:.c=.o)

^ explicitly declare the order in which to build
*/

/*
TODO:
further note when fuzzing is implemented:
use deterministic randomisation, with a
guaranteed seed - so e.g. don't use /dev/urandom
in test builds. e.g. just use normal rand()
but with a static seed e.g. 1234
*/
/*
TODO: stricter build flags, e.g.
CFLAGS += -fstack-protector-strong
CFLAGS += -fno-common
CFLAGS += -D_FORTIFY_SOURCE=2
CFLAGS += -fPIE

also consider:
-fstack-clash-protection
-Wl,-z,relro
-Wl,-z,now
*/

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifdef __OpenBSD__
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <sys/time.h>
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

typedef unsigned char u8;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

/* type asserts */
typedef char static_assert_char_is_8_bits[(CHAR_BIT == 8) ? 1 : -1];
typedef char static_assert_char_is_1[(sizeof(char) == 1) ? 1 : -1];
typedef char static_assert_u8_is_1[
    (sizeof(u8) == 1) ? 1 : -1];
typedef char static_assert_ushort_is_2[
    (sizeof(ushort) >= 2) ? 1 : -1];
typedef char static_assert_short_is_2[(sizeof(short) >= 2) ? 1 : -1];
typedef char static_assert_uint_is_4[
    (sizeof(uint) >= 4) ? 1 : -1];
typedef char static_assert_ulong_is_4[
    (sizeof(ulong) >= 4) ? 1 : -1];
typedef char static_assert_int_ge_32[(sizeof(int) >= 4) ? 1 : -1];
typedef char static_assert_twos_complement[
    ((-1 & 3) == 3) ? 1 : -1
];
typedef char assert_ulong_ptr[
    (sizeof(ulong) >= sizeof(void *)) ? 1 : -1
];
typedef char assert_size_t_ptr[
    (sizeof(size_t) >= sizeof(void *)) ? 1 : -1
];

/*
 * We set _FILE_OFFSET_BITS 64, but we only handle
 * files that are 128KB in size at a maximum, so we
 * realistically only need 32-bit at a minimum.
 *
 * We set 64 anyway, because there's no reason not
 * to, but some systems may ignore _FILE_OFFSET_BITS
 */
typedef char static_assert_off_t_is_32[(sizeof(off_t) >= 4) ? 1 : -1];

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

#ifndef O_NONBLOCK
#define O_NONBLOCK
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
 * Sanitize command tables.
 */
static void sanitize_command_list(void);
static void sanitize_command_index(size_t c);

/*
 * Argument handling (user input)
 */
static void set_cmd(int argc, char *argv[]);
static void set_cmd_args(int argc, char *argv[]);
static size_t conv_argv_part_num(const char *part_str);
static int xstrxcmp(const char *a, const char *b, size_t maxlen);

/*
 * Prep files for reading
 */
static void open_gbe_file(void);
static int lock_file(int fd);
static void xopen(int *fd, const char *path, int flags, struct stat *st);

/*
 * Read GbE file and verify
 * checksums.
 *
 * After this, we can run commands.
 */
static void copy_gbe(void);
static void read_checksums(void);
static int good_checksum(size_t partnum);

/*
 * Execute user command on GbE data.
 * These are stubs that call helpers.
 */
static void run_cmd(size_t c);
static void check_command_num(size_t c);
static u8 valid_command(size_t c);

/*
 * Helper functions for command: setmac
 */
static void cmd_helper_setmac(void);
static void parse_mac_string(void);
static size_t xstrxlen(const char *scmp, size_t maxlen);
static void set_mac_byte(size_t mac_byte_pos);
static void set_mac_nib(size_t mac_str_pos,
    size_t mac_byte_pos, size_t mac_nib_pos);
static ushort hextonum(char ch_s);
static ushort rhex(void);
static ushort read_urandom(void);
static ulong entropy_jitter(void);
static void write_mac_part(size_t partnum);

/*
 * Helper functions for command: dump
 */
static void cmd_helper_dump(void);
static void print_mac_from_nvm(size_t partnum);
static void hexdump(size_t partnum);

/*
 * Helper functions for command: swap
 */
static void cmd_helper_swap(void);

/*
 * Helper functions for command: copy
 */
static void cmd_helper_copy(void);

/*
 * Helper functions for commands:
 * cat, cat16 and cat128
 */
static void cmd_helper_cat(void);
static void cat_buf(u8 *b);

/*
 * After command processing, write
 * the modified GbE file back.
 *
 * These are stub functions: check
 * below for the actual functions.
 */
static void write_gbe_file(void);
static void set_checksum(size_t part);
static ushort calculated_checksum(size_t p);

/*
 * Helper functions for accessing
 * the NVM area during operation.
 */
static ushort nvm_word(size_t pos16, size_t part);
static void set_nvm_word(size_t pos16, size_t part, ushort val16);
static void set_part_modified(size_t p);
static void check_nvm_bound(size_t pos16, size_t part);
static void check_bin(size_t a, const char *a_name);

/*
 * Helper functions for stub functions
 * that handle GbE file reads/writes.
 */
static void rw_gbe_file_part(size_t p, int rw_type,
    const char *rw_type_str);
static void write_to_gbe_bin(void);
static int gbe_mv(void);
static void check_written_part(size_t p);
static void report_io_err_rw(void);
static int fsync_dir(const char *path);
static u8 *gbe_mem_offset(size_t part, const char *f_op);
static off_t gbe_file_offset(size_t part, const char *f_op);
static off_t gbe_x_offset(size_t part, const char *f_op,
    const char *d_type, off_t nsize, off_t ncmp);
static ssize_t rw_gbe_file_exact(int fd, u8 *mem, size_t nrw,
    off_t off, int rw_type);
static ssize_t rw_file_exact(int fd, u8 *mem, size_t len,
    off_t off, int rw_type, int loop_eagain, int loop_eintr,
    size_t max_retries, int off_reset);
static ssize_t prw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type, int loop_eagain, int loop_eintr,
    int off_reset);
static int io_args(int fd, void *mem, size_t nrw,
    off_t off, int rw_type);
static int check_file(int fd, struct stat *st);
static ssize_t rw_over_nrw(ssize_t r, size_t nrw);
#if !defined(HAVE_REAL_PREAD_PWRITE) || \
    HAVE_REAL_PREAD_PWRITE < 1
static off_t lseek_loop(int fd, off_t off,
    int whence, int loop_eagain, int loop_eintr);
#endif
static int try_err(int loop_err, int errval);

/*
 * Error handling and cleanup
 */
static void usage(void);
static void err(int nvm_errval, const char *msg, ...);
static int exit_cleanup(void);
static const char *getnvmprogname(void);

/*
 * a special kind of hell
 */
static char *new_tmpfile(int *fd, int local, const char *path);

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
static u8 real_buf[GBE_BUF_SIZE];
static u8 bufcmp[GBE_BUF_SIZE]; /* compare gbe/tmp/reads */
static u8 pad[GBE_WORK_SIZE]; /* the file that wouldn't die */
static u8 *buf = real_buf;

static ushort mac_buf[3];
static off_t gbe_file_size;
static off_t gbe_tmp_size;

static int gbe_fd = -1;
static size_t part;
static u8 part_modified[2];
static u8 part_valid[2];

static const char rmac[] = "xx:xx:xx:xx:xx:xx";
static const char *mac_str = rmac;
static const char *fname = NULL;
static const char *argv0;

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(~((size_t)1 << (sizeof(ssize_t)*CHAR_BIT-1))))
#endif

/*
 * Use these for .argc in command[]:
 */
#define ARGC_3 3
#define ARGC_4 4

#define NO_LOOP_EAGAIN 0
#define NO_LOOP_EINTR 0

enum {
	IO_READ,
	IO_WRITE,
	IO_PREAD,
	IO_PWRITE
};

/*
 * Used as indices for command[]
 * MUST be in the same order as entries in command[]
 */
enum {
	CMD_DUMP,
	CMD_SETMAC,
	CMD_SWAP,
	CMD_COPY,
	CMD_CAT,
	CMD_CAT16,
	CMD_CAT128
};

enum {
	ARG_NOPART,
	ARG_PART
};

enum {
	SKIP_CHECKSUM_READ,
	CHECKSUM_READ
};

enum {
	SKIP_CHECKSUM_WRITE,
	CHECKSUM_WRITE
};

struct commands {
	size_t chk;
	const char *str;
	void (*run)(void);
	int argc;
	u8 arg_part;
	u8 chksum_read;
	u8 chksum_write;
	size_t rw_size; /* within the 4KB GbE part */
	int flags; /* e.g. O_RDWR or O_RDONLY */
};

/*
 * Command table, for nvmutil commands
 */
static const struct commands command[] = {
	{ CMD_DUMP, "dump", cmd_helper_dump, ARGC_3,
	    ARG_NOPART,
	    SKIP_CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    NVM_SIZE, O_RDONLY },

	{ CMD_SETMAC, "setmac", cmd_helper_setmac, ARGC_3,
	    ARG_NOPART,
	    CHECKSUM_READ, CHECKSUM_WRITE,
	    NVM_SIZE, O_RDWR },

	{ CMD_SWAP, "swap", cmd_helper_swap, ARGC_3,
	    ARG_NOPART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDWR },

	{ CMD_COPY, "copy", cmd_helper_copy, ARGC_4,
	    ARG_PART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDWR },

	{ CMD_CAT, "cat", cmd_helper_cat, ARGC_3,
	    ARG_NOPART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDONLY },

	{ CMD_CAT16, "cat16", cmd_helper_cat, ARGC_3,
	    ARG_NOPART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDONLY },

	{ CMD_CAT128, "cat128", cmd_helper_cat, ARGC_3,
	    ARG_NOPART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDONLY },
};

#define MAX_CMD_LEN 50
#define N_COMMANDS items(command)
#define CMD_NULL N_COMMANDS

/*
 * Index in command[], will be set later
 */
static size_t cmd_index = CMD_NULL;

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

static int io_err_gbe = 0; /* intermediary write (verification) */
static int io_err_gbe_bin = 0; /* final write (real file) */
static int rw_check_err_read[] = {0, 0};
static int rw_check_partial_read[] = {0, 0};
static int rw_check_bad_part[] = {0, 0};

static int post_rw_checksum[] = {0, 0};

static dev_t gbe_dev;
static ino_t gbe_ino;

static dev_t tmp_dev;
static ino_t tmp_ino;

/*
 * No need to declare feature
 * macros. I jus declare the
 * prototypes. Should be safe
 * on most Unices and compilers
 */
int mkstemp(char *template);
int fchmod(int fd, mode_t mode);

static int tmp_fd = -1;
static char *tname = NULL;

int
main(int argc, char *argv[])
{
	argv0 = argv[0];
	if (argc < 3)
		usage();

	fname = argv[1];

#ifdef NVMUTIL_UNVEIL
	/*
	 * if global tmp is a different filesystem,
	 * unveil would trap on final file rename
	 * and we can't know the path in advance
	 */
	tname = new_tmpfile(&tmp_fd, 1, NULL);
#else
	tname = new_tmpfile(&tmp_fd, 0, NULL);
#endif

	if (tname == NULL)
		err(errno, "Can't create tmpfile");

#ifdef NVMUTIL_PLEDGE
#ifdef NVMUTIL_UNVEIL
	if (pledge("stdio flock rpath wpath cpath unveil", NULL) == -1)
		err(errno, "pledge, unveil");
	if (unveil("/dev/urandom", "r") == -1)
		err(errno, "unveil: /dev/urandom");
#else
	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		err(errno, "pledge");
#endif
#endif

	sanitize_command_list();

	set_cmd(argc, argv);
	set_cmd_args(argc, argv);

#ifdef NVMUTIL_UNVEIL
	if (command[cmd_index].flags == O_RDONLY) {
		if (unveil(fname, "r") == -1)
			err(errno, "%s: unveil r", fname);
	} else {
		if (unveil(fname, "rwc") == -1)
			err(errno, "%s: unveil rw", fname);
	}

	if (unveil(tname, "rwc") == -1)
		err(errno, "%s: unveil rwc", tname);

	if (unveil(NULL, NULL) == -1)
		err(errno, "unveil block (rw)");

	if (pledge("stdio flock rpath wpath cpath", NULL) == -1)
		err(errno, "pledge (kill unveil)");
#endif

	srand((uint)(time(NULL) ^ getpid()));

	open_gbe_file();

	memset(buf, 0, GBE_BUF_SIZE);
	memset(bufcmp, 0, GBE_BUF_SIZE);

	copy_gbe();

	read_checksums();

	run_cmd(cmd_index);

	if (command[cmd_index].flags == O_RDWR)
		write_to_gbe_bin();

	if (exit_cleanup() == -1)
		err(EIO, "%s: close", fname);

	if (io_err_gbe_bin)
		err(EIO, "%s: error writing final file");

	if (tname != NULL)
		free(tname);

	return EXIT_SUCCESS;
}

/*
 * Guard against regressions by maintainers (command table)
 */
static void
sanitize_command_list(void)
{
	size_t c;

	for (c = 0; c < N_COMMANDS; c++)
		sanitize_command_index(c);
}

/*
 * TODO: specific config checks per command
 */
static void
sanitize_command_index(size_t c)
{
	size_t gbe_rw_size;

	check_command_num(c);

	if (command[c].argc < 3)
		err(EINVAL, "cmd index %lu: argc below 3, %d",
		    (ulong)c, command[c].argc);

	if (command[c].str == NULL)
		err(EINVAL, "cmd index %lu: NULL str",
		    (ulong)c);
	if (*command[c].str == '\0')
		err(EINVAL, "cmd index %lu: empty str",
		    (ulong)c);

	if (xstrxlen(command[c].str, MAX_CMD_LEN + 1) >
	    MAX_CMD_LEN) {
		err(EINVAL, "cmd index %lu: str too long: %s",
		    (ulong)c, command[c].str);
	}

	if (command[c].run == NULL)
		err(EINVAL, "cmd index %lu: cmd ptr null",
		    (ulong)c);

	check_bin(command[c].arg_part, "cmd.arg_part");
	check_bin(command[c].chksum_read, "cmd.chksum_read");
	check_bin(command[c].chksum_write, "cmd.chksum_write");

	gbe_rw_size = command[c].rw_size;

	switch (gbe_rw_size) {
	case GBE_PART_SIZE:
	case NVM_SIZE:
		break;
	default:
		err(EINVAL, "Unsupported rw_size: %lu",
		    (ulong)gbe_rw_size);
	}

	if (gbe_rw_size > GBE_PART_SIZE)
		err(EINVAL, "rw_size larger than GbE part: %lu",
		    (ulong)gbe_rw_size);

	if (command[c].flags != O_RDONLY &&
	    command[c].flags != O_RDWR)
		err(EINVAL, "invalid cmd.flags setting");
}

static void
set_cmd(int argc, char *argv[])
{
	const char *cmd_str;

	for (cmd_index = 0; valid_command(cmd_index); cmd_index++) {
		cmd_str = command[cmd_index].str;

		if (xstrxcmp(argv[2], cmd_str, MAX_CMD_LEN) != 0)
			continue;
		else if (argc >= command[cmd_index].argc)
			return;

		err(EINVAL, "Too few args on command '%s'", cmd_str);
	}

	cmd_index = CMD_NULL;
}

static void
set_cmd_args(int argc, char *argv[])
{
	u8 arg_part;

	if (!valid_command(cmd_index) || argc < 3)
		usage();

	arg_part = command[cmd_index].arg_part;

	/* Maintainer bugs */
	if (arg_part && argc < 4)
		err(EINVAL,
		    "arg_part set for command that needs argc4");
	if (arg_part && cmd_index == CMD_SETMAC)
		err(EINVAL,
		    "arg_part set on CMD_SETMAC");

	if (cmd_index == CMD_SETMAC)
		mac_str = argc >= 4 ? argv[3] : rmac;
	else if (arg_part)
		part = conv_argv_part_num(argv[3]);
}

static size_t
conv_argv_part_num(const char *part_str)
{
	u8 ch;

	if (part_str[0] == '\0' || part_str[1] != '\0')
		err(EINVAL, "Partnum string '%s' wrong length", part_str);

	/* char signedness is implementation-defined */
	ch = (u8)part_str[0];
	if (ch < '0' || ch > '1')
		err(EINVAL, "Bad part number (%c)", ch);

	return (size_t)(ch - '0');
}

/*
 * Portable strcmp() but blocks NULL/empty/unterminated
 * strings. Even stricter than strncmp().
 */
static int
xstrxcmp(const char *a, const char *b, size_t maxlen)
{
	size_t i;

	if (a == NULL || b == NULL)
		err(EINVAL, "NULL input to xstrxcmp");

	if (*a == '\0' || *b == '\0')
		err(EINVAL, "Empty string in xstrxcmp");

	for (i = 0; i < maxlen; i++) {
		u8 ac = (u8)a[i];
		u8 bc = (u8)b[i];

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

static void
open_gbe_file(void)
{
	struct stat gbe_st;
	int flags;

	xopen(&gbe_fd, fname,
	    command[cmd_index].flags | O_BINARY |
	    O_NOFOLLOW | O_CLOEXEC, &gbe_st);

	/* inode will be checked later on write */
	gbe_dev = gbe_st.st_dev;
	gbe_ino = gbe_st.st_ino;

	if (gbe_st.st_nlink > 1)
		fprintf(stderr,
		    "%s: warning: file has %lu hard links\n",
		    fname, (ulong)gbe_st.st_nlink);

	if (gbe_st.st_nlink == 0)
		err(EIO, "%s: file unlinked while open", fname);

	flags = fcntl(gbe_fd, F_GETFL);
	if (flags == -1)
		err(errno, "%s: fcntl(F_GETFL)", fname);

	/*
	 * O_APPEND must not be used, because this
	 * allows POSIX write() to ignore the
	 * current write offset and write at EOF,
	 * which would therefore break pread/pwrite
	 */
	if (flags & O_APPEND)
		err(EIO, "%s: O_APPEND flag", fname);

	gbe_file_size = gbe_st.st_size;

	switch (gbe_file_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		break;
	default:
		err(EINVAL, "File size must be 8KB, 16KB or 128KB");
	}

	if (lock_file(gbe_fd) == -1)
		err(errno, "%s: can't lock", fname);
}

static int
lock_file(int fd)
{
	struct flock fl;

	memset(&fl, 0, sizeof(fl));

	if (command[cmd_index].flags == O_RDONLY)
		fl.l_type = F_RDLCK;
	else
		fl.l_type = F_WRLCK;

	fl.l_whence = SEEK_SET;

	if (fcntl(fd, F_SETLK, &fl) == -1)
		return -1;

	return 0;
}

static void
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
static void
copy_gbe(void)
{
	ssize_t r;
	struct stat st;

	/* read main file */
	r = rw_file_exact(gbe_fd, buf, gbe_file_size,
	    0, IO_PREAD, NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (r < 0)
		err(errno, "%s: read failed", fname);

	/* copy to tmpfile */

	r = rw_file_exact(tmp_fd, buf, gbe_file_size,
	    0, IO_PWRITE, NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (r < 0)
		err(errno, "%s: %s: copy failed",
		    fname, tname);

	/*
	 * file size comparison
	 */

	if (fstat(tmp_fd, &st) == -1)
		err(errno, "%s: stat", tname);

	gbe_tmp_size = st.st_size;

	if (gbe_tmp_size != gbe_file_size)
		err(EIO, "%s: %s: not the same size", fname, tname);

	/*
	 * fsync tmp gbe file, because we will compare
	 * its contents to what was read (for safety)
	 */
	if (fsync(tmp_fd) == -1)
		err(errno, "%s: fsync (tmpfile copy)", tname);

	r = rw_file_exact(tmp_fd, bufcmp, gbe_file_size,
	    0, IO_PREAD, NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (r < 0)
		err(errno, "%s: read failed (cmp)", tname);

	if (memcmp(buf, bufcmp, gbe_file_size) != 0)
		err(errno, "%s: %s: read contents differ (pre-test)",
		    fname, tname);

	/*
	 regular operations post-read operate only on the first
	 8KB, because each GbE part is the first 4KB of each
	 half of the file.

	 we no longer care about anything past 8KB, until we get
	 to writing, at which point we will flush the buffer
	 again
	 */

	if (gbe_file_size == SIZE_8KB)
		return;

	memcpy(buf + (size_t)GBE_PART_SIZE,
	    buf + (size_t)(gbe_file_size >> 1),
	    (size_t)GBE_PART_SIZE);
}

static void
read_checksums(void)
{
	size_t p;
	size_t skip_part;
	u8 arg_part;
	u8 num_invalid;
	u8 max_invalid;

	part_valid[0] = 0;
	part_valid[1] = 0;

	if (!command[cmd_index].chksum_read)
		return;

	num_invalid = 0;
	max_invalid = 2;

	arg_part = command[cmd_index].arg_part;
	if (arg_part)
		max_invalid = 1;

	/*
	 * Skip verification on this part,
	 * but only when arg_part is set.
	 */
	skip_part = part ^ 1;

	for (p = 0; p < 2; p++) {
		/*
		 * Only verify a part if it was *read*
		 */
		if (arg_part && (p == skip_part))
			continue;

		part_valid[p] = good_checksum(p);
		if (!part_valid[p])
			++num_invalid;
	}

	if (num_invalid >= max_invalid) {
		if (max_invalid == 1)
			err(ECANCELED, "%s: part %lu has a bad checksum",
			    fname, (ulong)part);
		err(ECANCELED, "%s: No valid checksum found in file",
		    fname);
	}
}

static int
good_checksum(size_t partnum)
{
	ushort expected_checksum = calculated_checksum(partnum);
	ushort current_checksum = nvm_word(NVM_CHECKSUM_WORD, partnum);

	if (current_checksum == expected_checksum)
		return 1;

	return 0;
}

static void
run_cmd(size_t c)
{
	check_command_num(c);

	if (command[c].run == NULL)
		err(EINVAL, "Command %lu: null ptr", (ulong)c);

	command[c].run();
}

static void
check_command_num(size_t c)
{
	if (!valid_command(c))
		err(EINVAL, "Invalid run_cmd arg: %lu",
		    (ulong)c);
}

static u8
valid_command(size_t c)
{
	if (c >= N_COMMANDS)
		return 0;

	if (c != command[c].chk)
		err(EINVAL, "Invalid cmd chk value (%lu) vs arg: %lu",
		    (ulong)command[c].chk, (ulong)c);

	return 1;
}

static void
cmd_helper_setmac(void)
{
	size_t partnum;

	printf("MAC address to be written: %s\n", mac_str);
	parse_mac_string();

	for (partnum = 0; partnum < 2; partnum++)
		write_mac_part(partnum);
}

static void
parse_mac_string(void)
{
	size_t mac_byte;

	if (xstrxlen(mac_str, 18) != 17)
		err(EINVAL, "MAC address is the wrong length");

	memset(mac_buf, 0, sizeof(mac_buf));

	for (mac_byte = 0; mac_byte < 6; mac_byte++)
		set_mac_byte(mac_byte);

	if ((mac_buf[0] | mac_buf[1] | mac_buf[2]) == 0)
		err(EINVAL, "Must not specify all-zeroes MAC address");

	if (mac_buf[0] & 1)
		err(EINVAL, "Must not specify multicast MAC address");
}

/*
 * strnlen() but aborts on NULL input, and empty strings.
 * Our version also prohibits unterminated strings.
 * strnlen() was standardized in POSIX.1-2008 and is not
 * available on some older systems, so we provide our own.
 */
static size_t
xstrxlen(const char *scmp, size_t maxlen)
{
	size_t xstr_index;

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

static void
set_mac_byte(size_t mac_byte_pos)
{
	size_t mac_str_pos = mac_byte_pos * 3;
	size_t mac_nib_pos;
	char separator;

	if (mac_str_pos < 15) {
		if ((separator = mac_str[mac_str_pos + 2]) != ':')
			err(EINVAL, "Invalid MAC address separator '%c'",
			    separator);
	}

	for (mac_nib_pos = 0; mac_nib_pos < 2; mac_nib_pos++)
		set_mac_nib(mac_str_pos, mac_byte_pos, mac_nib_pos);
}

static void
set_mac_nib(size_t mac_str_pos,
    size_t mac_byte_pos, size_t mac_nib_pos)
{
	char mac_ch;
	ushort hex_num;

	mac_ch = mac_str[mac_str_pos + mac_nib_pos];

	if ((hex_num = hextonum(mac_ch)) > 15)
		err(EINVAL, "Invalid character '%c'",
		    mac_str[mac_str_pos + mac_nib_pos]);

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
	mac_buf[mac_byte_pos >> 1] |= hex_num <<
	    (((mac_byte_pos & 1) << 3) /* left or right byte? */
	    | ((mac_nib_pos ^ 1) << 2)); /* left or right nib? */
}

static ushort
hextonum(char ch_s)
{
	u8 ch = (u8)ch_s;

	if ((uint)(ch - '0') <= 9)
		return ch - '0';

	ch |= 0x20;

	if ((uint)(ch - 'a') <= 5)
		return ch - 'a' + 10;

	if (ch == '?' || ch == 'x')
		return rhex(); /* random character */

	return 16; /* invalid character */
}

static ushort
rhex(void)
{
	struct timeval tv;
	ulong mix;
	static ulong counter = 0;
	ushort r;

	/* Read /dev/urandom
	 * if possible */
	r = read_urandom();
	if (r < 16)
		return r;

	/* Fallback */

	gettimeofday(&tv, NULL);

	mix = (ulong)tv.tv_sec
	    ^ (ulong)tv.tv_usec
	    ^ (ulong)getpid()
	    ^ (ulong)&mix
	    ^ counter++
	    ^ entropy_jitter();

	/*
	 * Stack addresses can vary between
	 * calls, thus increasing entropy.
	 */
	mix ^= (ulong)&mix;
	mix ^= (ulong)&tv;
	mix ^= (ulong)&counter;

	return (ushort)(mix & 0xf);
}

static ushort
read_urandom(void)
{
	static int fd = -1;
	static ssize_t n = -1;

	static u8 r[256];

	if (fd < 0) {

		fd = open("/dev/urandom", O_RDONLY | O_NONBLOCK);
		if (fd < 0) /* older openbsd */
			fd = open("/dev/arandom", O_RDONLY | O_NONBLOCK);
		if (fd < 0) /* super old unix (could block) */
			fd = open("/dev/random", O_RDONLY | O_NONBLOCK);

		if (fd < 0)
			return 16;
	}

	if (n < 0) {

		n = rw_file_exact(fd, r, 256, 0, IO_READ,
		    LOOP_EAGAIN, LOOP_EINTR, 2, OFF_ERR);

		if (n == 0)
			n = -1;
		if (n < 0)
			return 16;

		--n;
	}

	return r[n--] & 0xf;
}

static ulong
entropy_jitter(void)
{
	struct timeval a, b;
	ulong mix = 0;
	long mix_diff;
	int i;

	for (i = 0; i < 8; i++) {
		gettimeofday(&a, NULL);
		getpid();
		gettimeofday(&b, NULL);

		/*
		 * prevent negative numbers to prevent overflow,
		 * which would bias rand to large numbers
		 */
		mix_diff = (long)(b.tv_usec - a.tv_usec);
		if (mix_diff < 0)
			mix_diff = -mix_diff;

		mix ^= (ulong)(mix_diff);
		mix ^= (ulong)&mix;
	}

	return mix;
}

static void
write_mac_part(size_t partnum)
{
	size_t w;

	check_bin(partnum, "part number");
	if (!part_valid[partnum])
		return;

	for (w = 0; w < 3; w++)
		set_nvm_word(w, partnum, mac_buf[w]);

	printf("Wrote MAC address to part %lu: ",
	    (ulong)partnum);
	print_mac_from_nvm(partnum);
}

static void
cmd_helper_dump(void)
{
	size_t partnum;

	part_valid[0] = good_checksum(0);
	part_valid[1] = good_checksum(1);

	for (partnum = 0; partnum < 2; partnum++) {
		if (!part_valid[partnum])
			fprintf(stderr,
			    "BAD checksum %04x in part %lu (expected %04x)\n",
			    nvm_word(NVM_CHECKSUM_WORD, partnum),
			    (ulong)partnum,
			    calculated_checksum(partnum));

		printf("MAC (part %lu): ",
		    (ulong)partnum);
		print_mac_from_nvm(partnum);
		hexdump(partnum);
	}
}

static void
print_mac_from_nvm(size_t partnum)
{
	size_t c;
	ushort val16;

	for (c = 0; c < 3; c++) {
		val16 = nvm_word(c, partnum);
		printf("%02x:%02x",
		    (uint)(val16 & 0xff),
		    (uint)(val16 >> 8));
		if (c == 2)
			printf("\n");
		else
			printf(":");
	}
}

static void
hexdump(size_t partnum)
{
	size_t c;
	size_t row;
	ushort val16;

	for (row = 0; row < 8; row++) {
		printf("%08lx ", (ulong)((size_t)row << 4));
		for (c = 0; c < 8; c++) {
			val16 = nvm_word((row << 3) + c, partnum);
			if (c == 4)
				printf(" ");
			printf(" %02x %02x",
			    (uint)(val16 & 0xff),
			    (uint)(val16 >> 8));
		}
		printf("\n");
	}
}

static void
cmd_helper_swap(void)
{
	memcpy(
	    buf + (size_t)GBE_WORK_SIZE,
	    buf,
	    GBE_PART_SIZE);

	memcpy(
	    buf,
	    buf + (size_t)GBE_PART_SIZE,
	    GBE_PART_SIZE);

	memcpy(
	    buf + (size_t)GBE_PART_SIZE,
	    buf + (size_t)GBE_WORK_SIZE,
	    GBE_PART_SIZE);

	set_part_modified(0);
	set_part_modified(1);
}

static void
cmd_helper_copy(void)
{
	memcpy(
	    buf + (size_t)((part ^ 1) * GBE_PART_SIZE),
	    buf + (size_t)(part * GBE_PART_SIZE),
	    GBE_PART_SIZE);

	set_part_modified(part ^ 1);
}

static void
cmd_helper_cat(void)
{
	size_t p = 0;
	size_t ff = 0;
	size_t nff = 0;

	fflush(NULL);

	memset(pad, 0xff, GBE_PART_SIZE);

	switch (cmd_index) {
	case CMD_CAT:
		nff = 0;
		break;
	case CMD_CAT16:
		nff = 1;
		break;
	case CMD_CAT128:
		nff = 15;
		break;
	default:
		err(EINVAL, "erroneous call to cat");
	}

	for (p = 0; p < 2; p++) {
		cat_buf(bufcmp + (size_t)(p * (gbe_file_size >> 1)));

		for (ff = 0; ff < nff; ff++)
			cat_buf(pad);
	}
}

static void
cat_buf(u8 *b)
{
	if (rw_file_exact(STDOUT_FILENO, b,
	    GBE_PART_SIZE, 0, IO_WRITE, LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR) < 0)
		err(errno, "stdout: cat");
}

static void
write_gbe_file(void)
{
	struct stat gbe_st;
	struct stat tmp_st;

	size_t p;
	u8 update_checksum;

	if (command[cmd_index].flags == O_RDONLY)
		return;

	if (fstat(gbe_fd, &gbe_st) == -1)
		err(errno, "%s: re-check", fname);
	if (gbe_st.st_dev != gbe_dev || gbe_st.st_ino != gbe_ino)
		err(EIO, "%s: file replaced while open", fname);
	if (gbe_st.st_size != gbe_file_size)
		err(errno, "%s: file size changed before write", fname);
	if (!S_ISREG(gbe_st.st_mode))
		err(errno, "%s: file type changed before write", fname);

	if (fstat(tmp_fd, &tmp_st) == -1)
		err(errno, "%s: re-check", tname);
	if (tmp_st.st_dev != tmp_dev || tmp_st.st_ino != tmp_ino)
		err(EIO, "%s: file replaced while open", tname);
	if (tmp_st.st_size != gbe_file_size)
		err(errno, "%s: file size changed before write", tname);
	if (!S_ISREG(tmp_st.st_mode))
		err(errno, "%s: file type changed before write", tname);

	update_checksum = command[cmd_index].chksum_write;

	for (p = 0; p < 2; p++) {
		if (!part_modified[p])
			continue;

		if (update_checksum)
			set_checksum(p);

		rw_gbe_file_part(p, IO_PWRITE, "pwrite");
	}
}

static void
set_checksum(size_t p)
{
	check_bin(p, "part number");
	set_nvm_word(NVM_CHECKSUM_WORD, p, calculated_checksum(p));
}

static ushort
calculated_checksum(size_t p)
{
	size_t c;
	uint val16 = 0;

	for (c = 0; c < NVM_CHECKSUM_WORD; c++)
		val16 += (uint)nvm_word(c, p);

	return (ushort)((NVM_CHECKSUM - val16) & 0xffff);
}

/*
 * GbE NVM files store 16-bit (2-byte) little-endian words.
 * We must therefore swap the order when reading or writing.
 *
 * NOTE: The MAC address words are stored big-endian in the
 * file, but we assume otherwise and adapt accordingly.
 */

static ushort
nvm_word(size_t pos16, size_t p)
{
	size_t pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	return (ushort)buf[pos] |
	    ((ushort)buf[pos + 1] << 8);
}

static void
set_nvm_word(size_t pos16, size_t p, ushort val16)
{
	size_t pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	buf[pos] = (u8)(val16 & 0xff);
	buf[pos + 1] = (u8)(val16 >> 8);

	set_part_modified(p);
}

static void
set_part_modified(size_t p)
{
	check_bin(p, "part number");
	part_modified[p] = 1;
}

static void
check_nvm_bound(size_t c, size_t p)
{
	/*
	 * NVM_SIZE assumed as the limit, because this
	 * current design assumes that we will only
	 * ever modified the NVM area.
	 */

	check_bin(p, "part number");

	if (c >= NVM_WORDS)
		err(ECANCELED, "check_nvm_bound: out of bounds %lu",
		    (ulong)c);
}

static void
check_bin(size_t a, const char *a_name)
{
	if (a > 1)
		err(EINVAL, "%s must be 0 or 1, but is %lu",
		    a_name, (ulong)a);
}

static void
rw_gbe_file_part(size_t p, int rw_type,
    const char *rw_type_str)
{
	ssize_t r;
	size_t gbe_rw_size = command[cmd_index].rw_size;

	u8 *mem_offset;
	off_t file_offset;

	if (rw_type < IO_PREAD || rw_type > IO_PWRITE)
		err(errno, "%s: %s: part %lu: invalid rw_type, %d",
		    fname, rw_type_str, (ulong)p, rw_type);

	mem_offset = gbe_mem_offset(p, rw_type_str);
	file_offset = (off_t)gbe_file_offset(p, rw_type_str);

	r = rw_gbe_file_exact(tmp_fd, mem_offset,
	    gbe_rw_size, file_offset, rw_type);

	if (r == -1)
		err(errno, "%s: %s: part %lu",
		    fname, rw_type_str, (ulong)p);

	if ((size_t)r != gbe_rw_size)
		err(EIO, "%s: partial %s: part %lu",
		    fname, rw_type_str, (ulong)p);
}

static void
write_to_gbe_bin(void)
{
	int saved_errno;
	int mv;

	if (command[cmd_index].flags != O_RDWR)
		return;

	write_gbe_file();

	/*
	 * We may otherwise read from
	 * cache, so we must sync.
	 */
	if (fsync(tmp_fd) == -1)
		err(errno, "%s: fsync (pre-verification)",
		    tname);

	check_written_part(0);
	check_written_part(1);

	report_io_err_rw();

	if (io_err_gbe)
		err(EIO, "%s: bad write", fname);

	/*
	 * success!
	 * now just rename the tmpfile
	 */

	saved_errno = errno;

	if (close(tmp_fd) == -1) {
		fprintf(stderr, "FAIL: %s: close\n", tname);
		io_err_gbe_bin = 1;
	}

	if (close(gbe_fd) == -1) {
		fprintf(stderr, "FAIL: %s: close\n", fname);
		io_err_gbe_bin = 1;
	}

	errno = saved_errno;

	tmp_fd = -1;
	gbe_fd = -1;

	if (!io_err_gbe_bin) {

		mv = gbe_mv();

		if (mv < 0) {
			io_err_gbe_bin = 1;
			fprintf(stderr, "%s: %s\n",
			    fname, strerror(errno));
		} else {
			/*
			 * tmpfile removed
			 * by the rename
			 */

			if (tname != NULL)
				free(tname);

			tname = NULL;
		}
	}

	/*
	 * finally:
	 * must sync to disk!
	 * very nearly done
	 */

	if (!io_err_gbe_bin)
		return;

	fprintf(stderr, "FAIL (rename): %s: skipping fsync\n",
	    fname);
	if (errno)
		fprintf(stderr,
		    "errno %d: %s\n", errno, strerror(errno));
}

static void
check_written_part(size_t p)
{
	ssize_t r;
	size_t gbe_rw_size;
	u8 *mem_offset;
	off_t file_offset;
	u8 *buf_restore;
	struct stat st;

	if (!part_modified[p])
		return;

	gbe_rw_size = command[cmd_index].rw_size;

	/* invert not needed for pwrite */
	mem_offset = gbe_mem_offset(p, "pwrite");
	file_offset = (off_t)gbe_file_offset(p, "pwrite");

	memset(pad, 0xff, sizeof(pad));

	if (fstat(gbe_fd, &st) == -1)
		err(errno, "%s: fstat (post-write)", fname);
	if (st.st_dev != gbe_dev || st.st_ino != gbe_ino)
		err(EIO, "%s: file changed during write", fname);

	if (fstat(tmp_fd, &st) == -1)
		err(errno, "%s: fstat (post-write)", tname);
	if (st.st_dev != tmp_dev || st.st_ino != tmp_ino)
		err(EIO, "%s: file changed during write", tname);

	r = rw_gbe_file_exact(tmp_fd, pad,
	    gbe_rw_size, file_offset, IO_PREAD);

	if (r == -1)
		rw_check_err_read[p] = io_err_gbe = 1;
	else if ((size_t)r != gbe_rw_size)
		rw_check_partial_read[p] = io_err_gbe = 1;
	else if (memcmp(mem_offset, pad, gbe_rw_size) != 0)
		rw_check_bad_part[p] = io_err_gbe = 1;

	if (rw_check_err_read[p] ||
	    rw_check_partial_read[p])
		return;

	/*
	 * We only load one part on-file, into memory but
	 * always at offset zero, for post-write checks.
	 * That's why we hardcode good_checksum(0).
	 */
	buf_restore = buf;
	buf = pad;
	post_rw_checksum[p] = good_checksum(0);
	buf = buf_restore;
}

static void
report_io_err_rw(void)
{
	size_t p;

	if (!io_err_gbe)
		return;

	for (p = 0; p < 2; p++) {
		if (!part_modified[p])
			continue;

		if (rw_check_err_read[p])
			fprintf(stderr,
			    "%s: pread: p%lu (post-verification)\n",
			    fname, (ulong)p);
		if (rw_check_partial_read[p])
			fprintf(stderr,
			    "%s: partial pread: p%lu (post-verification)\n",
			    fname, (ulong)p);
		if (rw_check_bad_part[p])
			fprintf(stderr,
			    "%s: pwrite: corrupt write on p%lu\n",
			    fname, (ulong)p);

		if (rw_check_err_read[p] ||
		    rw_check_partial_read[p]) {
			fprintf(stderr,
			    "%s: p%lu: skipped checksum verification "
			    "(because read failed)\n",
			    fname, (ulong)p);

			continue;
		}

		fprintf(stderr, "%s: ", fname);

		if (post_rw_checksum[p])
			fprintf(stderr, "GOOD");
		else
			fprintf(stderr, "BAD");

		fprintf(stderr, " checksum in p%lu on-disk.\n",
		    (ulong)p);

		if (post_rw_checksum[p]) {
			fprintf(stderr,
			    "    This does NOT mean it's safe. it may be\n"
			    "    salvageable if you use the cat feature.\n");
		}
	}
}

static int
gbe_mv(void)
{
	int r;
	int saved_errno;
	int tmp_gbe_bin_exists = 1;

	char *dest_tmp = NULL;
	int dest_fd = -1;

	saved_errno = errno;

	r = rename(tname, fname);

	if (r > -1) {
		/*
		 * same filesystem
		 */

		tmp_gbe_bin_exists = 0;

		if (fsync_dir(fname) < 0)
			r = -1;

		goto ret_gbe_mv;
	}

	if (errno != EXDEV)
		goto ret_gbe_mv;

	/* cross-filesystem rename */

	if ((r = tmp_fd = open(tname,
	    O_RDONLY | O_BINARY)) == -1)
		goto ret_gbe_mv;

	/* create replacement temp in target directory */
	dest_tmp = new_tmpfile(&dest_fd, 1, fname);
	if (dest_tmp == NULL)
		goto ret_gbe_mv;

	/* copy data */

	r = rw_file_exact(tmp_fd, bufcmp,
	    gbe_file_size, 0, IO_PREAD,
	    NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (r < 0)
		goto ret_gbe_mv;

	r = rw_file_exact(dest_fd, bufcmp,
	    gbe_file_size, 0, IO_PWRITE,
	    NO_LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR);

	if (r < 0)
		goto ret_gbe_mv;

	if (fsync(dest_fd) == -1)
		goto ret_gbe_mv;

	if (close(dest_fd) == -1)
		goto ret_gbe_mv;

	if (rename(dest_tmp, fname) == -1)
		goto ret_gbe_mv;

	if (fsync_dir(fname) < 0)
		goto ret_gbe_mv;

	free(dest_tmp);
	dest_tmp = NULL;

ret_gbe_mv:

	if (gbe_fd > -1) {
		if (close(gbe_fd) < 0)
			r = -1;
		if (fsync_dir(fname) < 0)
			r = -1;
		gbe_fd = -1;
	}

	if (tmp_fd > -1) {
		if (close(tmp_fd) < 0)
			r = -1;

		tmp_fd = -1;
	}

	/*
	 * before this function is called,
	 * tmp_fd may have been moved
	 */
	if (tmp_gbe_bin_exists) {
		if (unlink(tname) < 0)
			r = -1;
		else
			tmp_gbe_bin_exists = 0;
	}

	if (r < 0) {
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

	return r;
}

/*
 * Ensure rename() is durable by syncing the
 * directory containing the target file.
 */
static int
fsync_dir(const char *path)
{
#if defined(PATH_LEN) && \
    (PATH_LEN) >= 256
	size_t maxlen = PATH_LEN;
#else
	size_t maxlen = 4096;
#endif
	size_t pathlen;
/*	char dirbuf[maxlen]; */
	char *dirbuf = NULL;
	char *slash;
	int dfd = -1;

	struct stat st;

	int saved_errno = errno;

	pathlen = xstrxlen(path, maxlen);

	if (pathlen >= maxlen) {
		fprintf(stderr, "Path too long for fsync_parent_dir\n");
		goto err_fsync_dir;
	}

	dirbuf = malloc(pathlen + 1);
	if (dirbuf == NULL)
		goto err_fsync_dir;

	memcpy(dirbuf, path, pathlen + 1);
	slash = strrchr(dirbuf, '/');

	if (slash != NULL) {
		*slash = '\0';
		if (*dirbuf == '\0')
			strcpy(dirbuf, "/");
	} else {
		strcpy(dirbuf, ".");
	}

	dfd = open(dirbuf, O_RDONLY);
	if (dfd == -1)
		goto err_fsync_dir;

	if (fstat(dfd, &st) < 0)
		goto err_fsync_dir;

	if (!S_ISDIR(st.st_mode)) {
		fprintf(stderr, "%s: not a directory\n", dirbuf);
		goto err_fsync_dir;
	}

	/* sync file on disk */
	if (fsync(dfd) == -1)
		goto err_fsync_dir;

	if (close(dfd) == -1)
		goto err_fsync_dir;

	if (dirbuf != NULL)
		free(dirbuf);

	errno = saved_errno;
	return 0;

err_fsync_dir:
	if (!errno)
		errno = EIO;

	if (errno != saved_errno)
		fprintf(stderr, "%s: %s\n", fname, strerror(errno));

	if (dirbuf != NULL)
		free(dirbuf);

	if (dfd > -1)
		close(dfd);

	io_err_gbe_bin = 1;
	errno = saved_errno;

	return -1;
}

/*
 * This one is similar to gbe_file_offset,
 * but used to check Gbe bounds in memory,
 * and it is *also* used during file I/O.
 */
static u8 *
gbe_mem_offset(size_t p, const char *f_op)
{
	off_t gbe_off = gbe_x_offset(p, f_op, "mem",
	    GBE_PART_SIZE, GBE_WORK_SIZE);

	return (u8 *)(buf + (size_t)gbe_off);
}

/*
 * I/O operations filtered here. These operations must
 * only write from the 0th position or the half position
 * within the GbE file, and write 4KB of data.
 *
 * This check is called, to ensure just that.
 */
static off_t
gbe_file_offset(size_t p, const char *f_op)
{
	off_t gbe_file_half_size = gbe_file_size >> 1;

	return gbe_x_offset(p, f_op, "file",
	    gbe_file_half_size, gbe_file_size);
}

static off_t
gbe_x_offset(size_t p, const char *f_op, const char *d_type,
    off_t nsize, off_t ncmp)
{
	off_t off;

	check_bin(p, "part number");

	off = ((off_t)p) * (off_t)nsize;

	if (off > ncmp - GBE_PART_SIZE)
		err(ECANCELED, "%s: GbE %s %s out of bounds",
		    fname, d_type, f_op);

	if (off != 0 && off != ncmp >> 1)
		err(ECANCELED, "%s: GbE %s %s at bad offset",
		    fname, d_type, f_op);

	return off;
}

static ssize_t
rw_gbe_file_exact(int fd, u8 *mem, size_t nrw,
    off_t off, int rw_type)
{
	size_t mem_addr;
	size_t buf_addr;
	ssize_t r;

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		return -1;

	mem_addr = (size_t)(void *)mem;
	buf_addr = (size_t)(void *)buf;

	if (mem != (void *)pad) {
		if (mem_addr < buf_addr)
			goto err_rw_gbe_file_exact;

		if ((mem_addr - buf_addr) >= (size_t)GBE_WORK_SIZE)
			goto err_rw_gbe_file_exact;
	}

	if (off < 0 || off >= gbe_file_size)
		goto err_rw_gbe_file_exact;

	if (nrw > (size_t)(gbe_file_size - off))
		goto err_rw_gbe_file_exact;

	if (nrw > (size_t)GBE_PART_SIZE)
		goto err_rw_gbe_file_exact;

	r = rw_file_exact(fd, mem, nrw, off, rw_type,
	    NO_LOOP_EAGAIN, LOOP_EINTR, MAX_ZERO_RW_RETRY,
	    OFF_ERR);

	return rw_over_nrw(r, nrw);

err_rw_gbe_file_exact:
	errno = EIO;
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
static ssize_t
rw_file_exact(int fd, u8 *mem, size_t nrw,
    off_t off, int rw_type, int loop_eagain,
    int loop_eintr, size_t max_retries,
    int off_reset)
{
	ssize_t rv = 0;
	ssize_t rc = 0;
	size_t retries_on_zero = 0;
	off_t off_cur;
	size_t nrw_cur;
	void *mem_cur;

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		return -1;

	while (1) {
	
		/* Prevent theoretical overflow */
		if (rv >= 0 && (size_t)rv > (nrw - rc))
			goto err_rw_file_exact;

		rc += rv;
		if ((size_t)rc >= nrw)
			break;

		mem_cur = (void *)(mem + (size_t)rc);
		nrw_cur = (size_t)(nrw - (size_t)rc);
		if (off < 0)
			goto err_rw_file_exact;
		off_cur = off + (off_t)rc;

		rv = prw(fd, mem_cur, nrw_cur, off_cur,
		    rw_type, loop_eagain, loop_eintr,
		    off_reset);

		if (rv < 0)
			return -1;

		if (rv == 0) {
			if (retries_on_zero++ < max_retries)
				continue;
			goto err_rw_file_exact;
		}

		retries_on_zero = 0;
	}

	if ((size_t)rc != nrw)
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

static ssize_t
prw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type,
    int loop_eagain, int loop_eintr,
    int off_reset)
{
	ssize_t r;
	int positional_rw;
	struct stat st;
#if !defined(HAVE_REAL_PREAD_PWRITE) || \
    HAVE_REAL_PREAD_PWRITE < 1
	int saved_errno;
	off_t verified;
	off_t off_orig;
	off_t off_last;
#endif

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		return -1;

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
		    (errno == try_err(loop_eintr, EINTR)
		    || errno == try_err(loop_eagain, EAGAIN)));
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

static int
io_args(int fd, void *mem, size_t nrw,
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
	if (nrw > (size_t)SSIZE_MAX)
		goto err_io_args;

	/* prevent overflow */
	if (((size_t)off + nrw) < (size_t)off)
		goto err_io_args;

	if (rw_type > IO_PWRITE)
		goto err_io_args;

	return 0;

err_io_args:
	errno = EIO;
	return -1;
}

static int
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
static ssize_t
rw_over_nrw(ssize_t r, size_t nrw)
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

	if ((size_t)r > SSIZE_MAX) {
		/*
		 * Theoretical buggy libc
		 * check. Extremely academic.
		 *
		 * Specifications never
		 * allow this return value
		 * to exceed SSIZE_MAX, but
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
	if ((size_t)r > nrw)
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
static off_t
lseek_loop(int fd, off_t off, int whence,
    int loop_eagain, int loop_eintr)
{
	off_t old = -1;

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
static int
try_err(int loop_err, int errval)
{
	if (loop_err)
		return errval;

	/* errno is never negative,
	   so functions checking it
	   can use it accordingly */
	return -1;
}

static void
usage(void)
{
	const char *util = getnvmprogname();

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

static void
err(int nvm_errval, const char *msg, ...)
{
	va_list args;

	if (errno == 0)
		errno = nvm_errval;
	if (!errno)
		errno = ECANCELED;

	(void)exit_cleanup();

	fprintf(stderr, "%s: ", getnvmprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	fprintf(stderr, ": %s", strerror(errno));

	fprintf(stderr, "\n");

	if (tname != NULL)
		free(tname);

	exit(EXIT_FAILURE);
}

static int
exit_cleanup(void)
{
	int close_err = 0;
	int saved_errno = errno;

	if (gbe_fd > -1) {
		if (close(gbe_fd) == -1)
			close_err = 1;
		gbe_fd = -1;
	}

	if (tmp_fd > -1) {
		if (close(tmp_fd) == -1)
			close_err = 1;
	}

	if (tname != NULL) {
		if (unlink(tname) == -1)
			close_err = 1;
	}

	tmp_fd = -1;

	if (saved_errno)
		errno = saved_errno;

	if (close_err)
		return -1;

	return 0;
}

static const char *
getnvmprogname(void)
{
	const char *p;

	if (argv0 == NULL || *argv0 == '\0')
		return "";

	p = strrchr(argv0, '/');

	if (p)
		return p + 1;
	else
		return argv0;
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
static char *
new_tmpfile(int *fd, int local, const char *path)
{
	size_t maxlen;
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

	size_t tmpdir_len = 0;
	size_t tmpname_len = 0;
	size_t tmppath_len = 0;

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
	maxlen = 4096;
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
		base = getenv("TMPDIR");

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

		memcpy(dest + (size_t)1, tmpname, tmpname_len);

		memcpy(dest + (size_t)1 + tmpname_len,
		    default_tmpname, tmpdir_len);
	} else {

		memcpy(dest, base, tmpdir_len);

		dest[tmpdir_len] = '/';

		memcpy(dest + tmpdir_len + 1, tmpname, tmpname_len);
	}

	dest[tmppath_len] = '\0';

	fd_tmp = mkstemp(dest);
	if (fd_tmp == -1)
		goto err_new_tmpfile;

	if (fchmod(fd_tmp, 0600) == -1)
		goto err_new_tmpfile;

	if (lock_file(fd_tmp) == -1)
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

	/* inode will be checked later on write */
	tmp_dev = st.st_dev;
	tmp_ino = st.st_ino;

	/* tmpfile has >1 hardlinks */
	if (st.st_nlink > 1)
		goto err_new_tmpfile;

	/* tmpfile unlinked while opened */
	if (st.st_nlink == 0)
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

	*fd = fd_tmp;

	return dest;

err_new_tmpfile:

	if (dest != NULL)
		free(dest);

	if (fd_tmp > -1)
		close(fd_tmp);

	return NULL;
}
