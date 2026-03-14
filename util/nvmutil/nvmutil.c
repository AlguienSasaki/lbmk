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
 * I/O config (build-time)
 *
 * Regarding:
 * Retries on zero-return.
 *
 * 10 retries is generous,
 * but also conservative.
 * This is enough for e.g.
 * slow USB flash drives,
 * busy NFS servers, etc.
 * Any more is too much
 * and not of much benefit.
 *
 * At least 3-5 recommended.
 * Pass this at build time.
 */
#ifndef MAX_ZERO_RW_RETRY
#define MAX_ZERO_RW_RETRY 10
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

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
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
 *
 * Portability: /dev/urandom used
 * on Linux / old Unix, whereas
 * arc4random is used on BSD/MacOS.
 */
static void open_dev_urandom(void);
static void open_gbe_file(void);
static void lock_gbe_file(void);
static void xopen(int *fd, const char *path, int flags, struct stat *st);

/*
 * Read GbE file and verify
 * checksums.
 *
 * After this, we can run commands.
 */
static void read_gbe_file(void);
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
static ushort fallback_rand(void);
static ulong entropy_jitter(void);
static void write_mac_part(size_t partnum);

/*
 * Helper functions for command: dump
 */
static void cmd_helper_dump(void);
static void print_mac_from_nvm(size_t partnum);
static void hexdump(size_t partnum);

/*
 * Helper functions for commands:
 * cat, cat16 and cat128
 */
static void cmd_helper_cat(void);
static void gbe_cat_buf(u8 *b);

/*
 * After command processing, write
 * the modified GbE file back.
 *
 * These are stub functions: check
 * below for the actual functions.
 */
static void write_gbe_file(void);
static void override_part_modified(void);
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
static void check_written_part(size_t p);
static void report_io_err_rw(void);
static u8 *gbe_mem_offset(size_t part, const char *f_op);
static off_t gbe_file_offset(size_t part, const char *f_op);
static off_t gbe_x_offset(size_t part, const char *f_op,
    const char *d_type, off_t nsize, off_t ncmp);
static ssize_t rw_gbe_file_exact(int fd, u8 *mem, size_t nrw,
    off_t off, int rw_type);
static ssize_t rw_file_exact(int fd, u8 *mem, size_t len,
    off_t off, int rw_type, int loop_eagain, int loop_eintr,
    size_t max_retries);
static ssize_t rw_file_once(int fd, u8 *mem, size_t len,
    off_t off, int rw_type, size_t rc, int loop_eagain,
    int loop_eintr, size_t max_retries);
static ssize_t prw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type, int loop_eagain, int loop_eintr);
static int check_file(int fd, struct stat *st);
static int rw_over_nrw(ssize_t r, size_t nrw);
static off_t lseek_loop(int fd, off_t off,
    int whence, int loop_eagain, int loop_eintr);
static int try_err(int loop_err, int errval);

/*
 * Error handling and cleanup
 */
static int close_files(void);
static void err(int nvm_errval, const char *msg, ...);
static const char *getnvmprogname(void);
static void usage(int usage_exit);

/*
 * Sizes in bytes:
 */
#define SIZE_1KB 1024
#define SIZE_4KB (4 * SIZE_1KB)
#define SIZE_8KB (8 * SIZE_1KB)
#define SIZE_16KB (16 * SIZE_1KB)
#define SIZE_128KB (128 * SIZE_1KB)

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
#define GBE_FILE_SIZE SIZE_8KB /* for buf */
#define GBE_PART_SIZE (GBE_FILE_SIZE >> 1)
#define NVM_CHECKSUM 0xBABA
#define NVM_SIZE 128
#define NVM_WORDS (NVM_SIZE >> 1)
#define NVM_CHECKSUM_WORD (NVM_WORDS - 1)

#define NUM_RANDOM_BYTES 12
static u8 rnum[NUM_RANDOM_BYTES];

/*
 * Portable macro based on BSD nitems.
 * Used to count the number of commands (see below).
 */
#define items(x) (sizeof((x)) / sizeof((x)[0]))

static const char newrandom[] = "/dev/urandom";
static const char *rname = NULL;

/*
 * GbE files can be 8KB, 16KB or 128KB,
 * but we only need the two 4KB parts
 * from offset zero and offset 64KB in
 * a 128KB file, or zero and 8KB in a 16KB
 * file, or zero and 4KB in an 8KB file.
 *
 * The code will handle this properly.
 */
static u8 real_buf[GBE_FILE_SIZE];
static u8 pad[GBE_FILE_SIZE]; /* the file that wouldn't die */
static u8 *buf = real_buf;

static ushort mac_buf[3];
static off_t gbe_file_size;

static int urandom_fd = -1;
static int gbe_fd = -1;
static size_t part;
static u8 part_modified[2];
static u8 part_valid[2];

static const char rmac[] = "xx:xx:xx:xx:xx:xx";
static const char *mac_str;
static const char *fname;
static const char *argv0;

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(~((size_t)1 << (sizeof(ssize_t)*CHAR_BIT-1))))
#endif

/*
 * Use these for .invert in command[]:
 * If set to 1: read/write inverter (p0->p1, p1->p0)
 */
#define PART_INVERT 1
#define NO_INVERT 0

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

/*
 * If set, a given part will always be written.
 */
enum {
	SET_MOD_OFF, /* don't manually set part modified */
	SET_MOD_0, /* set part 0 modified */
	SET_MOD_1, /* set part 1 modified */
	SET_MOD_N, /* set user-specified part modified */
		/* affected by command[].invert */
	SET_MOD_BOTH /* set both parts modified */
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
	u8 invert;
	u8 set_modified;
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
	    NO_INVERT, SET_MOD_OFF,
	    ARG_NOPART,
	    SKIP_CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    NVM_SIZE, O_RDONLY },

	{ CMD_SETMAC, "setmac", cmd_helper_setmac, ARGC_3,
	    NO_INVERT, SET_MOD_OFF,
	    ARG_NOPART,
	    CHECKSUM_READ, CHECKSUM_WRITE,
	    NVM_SIZE, O_RDWR },

	/*
	 * OPTIMISATION: Read inverted, so no copying is needed.
	 */
	{ CMD_SWAP, "swap", NULL, ARGC_3,
	    PART_INVERT, SET_MOD_BOTH,
	    ARG_NOPART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDWR },

	/*
	 * OPTIMISATION: Read inverted, so no copying is needed.
	 * The non-target part will not be read.
	 */
	{ CMD_COPY, "copy", NULL, ARGC_4,
	    PART_INVERT, SET_MOD_N,
	    ARG_PART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDWR },

	{ CMD_CAT, "cat", cmd_helper_cat, ARGC_3,
	    NO_INVERT, SET_MOD_OFF,
	    ARG_NOPART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDONLY },

	{ CMD_CAT16, "cat16", cmd_helper_cat, ARGC_3,
	    NO_INVERT, SET_MOD_OFF,
	    ARG_NOPART,
	    CHECKSUM_READ, SKIP_CHECKSUM_WRITE,
	    GBE_PART_SIZE, O_RDONLY },

	{ CMD_CAT128, "cat128", cmd_helper_cat, ARGC_3,
	    NO_INVERT, SET_MOD_OFF,
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
typedef char assert_rand_byte[(NUM_RANDOM_BYTES>0)?1:-1];
typedef char assert_rand_len[(NUM_RANDOM_BYTES<NVM_SIZE)?1:-1];
/* commands */
typedef char assert_cmd_dump[(CMD_DUMP==0)?1:-1];
typedef char assert_cmd_setmac[(CMD_SETMAC==1)?1:-1];
typedef char assert_cmd_swap[(CMD_SWAP==2)?1:-1];
typedef char assert_cmd_copy[(CMD_COPY==3)?1:-1];
typedef char assert_cmd_cat[(CMD_CAT==4)?1:-1];
typedef char assert_cmd_cat16[(CMD_CAT16==5)?1:-1];
typedef char assert_cmd_cat128[(CMD_CAT128==6)?1:-1];
/* mod_type */
typedef char assert_mod_off[(SET_MOD_OFF==0)?1:-1];
typedef char assert_mod_0[(SET_MOD_0==1)?1:-1];
typedef char assert_mod_1[(SET_MOD_1==2)?1:-1];
typedef char assert_mod_n[(SET_MOD_N==3)?1:-1];
typedef char assert_mod_both[(SET_MOD_BOTH==4)?1:-1];
/* bool */
typedef char bool_arg_nopart[(ARG_NOPART==0)?1:-1];
typedef char bool_arg_part[(ARG_PART==1)?1:-1];
typedef char bool_skip_checksum_read[(SKIP_CHECKSUM_READ==0)?1:-1];
typedef char bool_checksum_read[(CHECKSUM_READ==1)?1:-1];
typedef char bool_skip_checksum_write[(SKIP_CHECKSUM_WRITE==0)?1:-1];
typedef char bool_checksum_write[(CHECKSUM_WRITE==1)?1:-1];
typedef char bool_no_invert[(NO_INVERT==0)?1:-1];
typedef char bool_part_invert[(PART_INVERT==1)?1:-1];
typedef char bool_loop_eintr[(LOOP_EINTR==1||LOOP_EINTR==0)?1:-1];
typedef char bool_loop_eagain[(LOOP_EAGAIN==1||LOOP_EAGAIN==0)?1:-1];
typedef char bool_no_loop_eintr[(NO_LOOP_EINTR==0)?1:-1];
typedef char bool_no_loop_eagain[(NO_LOOP_EAGAIN==0)?1:-1];

static int use_prng = 0;

static int io_err_gbe = 0;
static int rw_check_err_read[] = {0, 0};
static int rw_check_partial_read[] = {0, 0};
static int rw_check_bad_part[] = {0, 0};

static int post_rw_checksum[] = {0, 0};

int
main(int argc, char *argv[])
{
	argv0 = argv[0];
	if (argc < 3)
		usage(1);

	fname = argv[1];

#ifdef NVMUTIL_PLEDGE
#ifdef NVMUTIL_UNVEIL
	if (pledge("stdio rpath wpath unveil", NULL) == -1)
		err(errno, "pledge");
	if (unveil("/dev/urandom", "r") == -1)
		err(errno, "unveil /dev/urandom");
#else
	if (pledge("stdio rpath wpath", NULL) == -1)
		err(errno, "pledge");
#endif
#endif

	sanitize_command_list();

	set_cmd(argc, argv);
	set_cmd_args(argc, argv);

#ifdef NVMUTIL_PLEDGE
#ifdef NVMUTIL_UNVEIL
	if (command[cmd_index].flags == O_RDONLY) {
		if (unveil(fname, "r") == -1)
			err(errno, "%s: unveil ro", fname);
		if (unveil(NULL, NULL) == -1)
			err(errno, "unveil block (ro)");
		if (pledge("stdio rpath", NULL) == -1)
			err(errno, "pledge ro (kill unveil)");
	} else {
		if (unveil(fname, "rw") == -1)
			err(errno, "%s: unveil rw", fname);
		if (unveil(NULL, NULL) == -1)
			err(errno, "unveil block (rw)");
		if (pledge("stdio rpath wpath", NULL) == -1)
			err(errno, "pledge rw (kill unveil)");
	}
#else
	if (command[cmd_index].flags == O_RDONLY) {
		if (pledge("stdio rpath", NULL) == -1)
			err(errno, "pledge ro");
	}
#endif
#endif

	open_dev_urandom();

	open_gbe_file();
	lock_gbe_file();

#ifdef NVMUTIL_PLEDGE
	if (pledge("stdio", NULL) == -1)
		err(errno, "pledge stdio (main)");
#endif

	/*
	 * Used by CMD_CAT, for padding
	 */
	memset(pad, 0xff, sizeof(pad));

	read_gbe_file();
	read_checksums();

	run_cmd(cmd_index);

	if (command[cmd_index].flags == O_RDWR) {

		write_gbe_file();

		/*
		 * We may otherwise read from
		 * cache, so we must sync.
		 */
		if (fsync(gbe_fd) == -1)
			err(errno, "%s: fsync (pre-verification)",
			    fname);

		check_written_part(0);
		check_written_part(1);

		report_io_err_rw();

		if (io_err_gbe)
			err(EIO, "%s: bad write", fname);
	}

	if (close_files() == -1)
		err(EIO, "%s: close", fname);

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
	u8 mod_type;
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

	mod_type = command[c].set_modified;
	switch (mod_type) {
	case SET_MOD_0:
	case SET_MOD_1:
	case SET_MOD_N:
	case SET_MOD_BOTH:
	case SET_MOD_OFF:
		break;
	default:
		err(EINVAL, "Unsupported set_mod type: %u", mod_type);
	}

	check_bin(command[c].invert, "cmd.invert");
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
		usage(1);

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
		if (a[i] != b[i])
			return (u8)a[i] - (u8)b[i];

		if (a[i] == '\0')
			return 0;
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
open_dev_urandom(void)
{
	rname = newrandom;
	urandom_fd = open(rname, O_RDONLY);
	if (urandom_fd != -1)
		return;

	/* fallback on VERY VERY VERY old unix */
	use_prng = 1;
	srand((uint)(time(NULL) ^ getpid()));
}

static void
open_gbe_file(void)
{
	struct stat gbe_st;
	int flags;

	xopen(&gbe_fd, fname,
	    command[cmd_index].flags | O_BINARY | O_NOFOLLOW, &gbe_st);

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
		err(EIO, "%s: O_APPEND flag");

	gbe_file_size = gbe_st.st_size;

	switch (gbe_file_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		break;
	default:
		err(EINVAL, "File size must be 8KB, 16KB or 128KB");
	}
}

static void
lock_gbe_file(void)
{
	struct flock fl;

	memset(&fl, 0, sizeof(fl));

	if (command[cmd_index].flags == O_RDONLY)
		fl.l_type = F_RDLCK;
	else
		fl.l_type = F_WRLCK;

	fl.l_whence = SEEK_SET;

	if (fcntl(gbe_fd, F_SETLK, &fl) == -1)
		err(errno, "file is locked by another process");
}

static void
xopen(int *fd_ptr, const char *path, int flags, struct stat *st)
{
	if ((*fd_ptr = open(path, flags)) == -1)
		err(errno, "%s", path);

	if (fstat(*fd_ptr, st) == -1)
		err(errno, "%s", path);

	if (!S_ISREG(st->st_mode))
		err(errno, "%s: not a regular file", path);
}

static void
read_gbe_file(void)
{
	size_t p;
	u8 do_read[2] = {1, 1};

	/*
	 * Commands specifying a partnum only
	 * need the given GbE part to be read.
	 */
	if (command[cmd_index].arg_part)
		do_read[part ^ 1] = 0;

	for (p = 0; p < 2; p++) {
		if (do_read[p])
			rw_gbe_file_part(p, IO_PREAD, "pread");
	}
}

static void
read_checksums(void)
{
	size_t p;
	size_t skip_part;
	u8 invert;
	u8 arg_part;
	u8 num_invalid;
	u8 max_invalid;

	part_valid[0] = 0;
	part_valid[1] = 0;

	if (!command[cmd_index].chksum_read)
		return;

	num_invalid = 0;
	max_invalid = 2;

	invert = command[cmd_index].invert;
	arg_part = command[cmd_index].arg_part;
	if (arg_part)
		max_invalid = 1;

	/*
	 * Skip verification on this part,
	 * but only when arg_part is set.
	 */
	skip_part = part ^ 1 ^ invert;

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
	if (command[c].run != NULL)
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
	static size_t n = 0;

	if (use_prng)
		return fallback_rand();

	if (!n) {
		n = sizeof(rnum);
		if (rw_file_exact(urandom_fd, rnum, n, 0, IO_READ,
		    NO_LOOP_EAGAIN, LOOP_EINTR, MAX_ZERO_RW_RETRY) == -1)
			err(errno, "Randomisation failed");
	}

	return (ushort)(rnum[--n] & 0xf);
}

static ushort
fallback_rand(void)
{
	struct timeval tv;
	ulong mix;
	static ulong counter = 0;

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
cmd_helper_cat(void)
{
	size_t p;
	size_t ff;
	size_t n = 0;

	if (cmd_index == CMD_CAT16)
		n = 1;
	else if (cmd_index == CMD_CAT128)
		n = 15;
	else if (cmd_index != CMD_CAT)
		err(EINVAL, "cmd_helper_cat called erroneously");

	fflush(NULL);

	for (p = 0; p < 2; p++) {
		gbe_cat_buf(buf + (p * GBE_PART_SIZE));

		for (ff = 0; ff < n; ff++)
			gbe_cat_buf(pad);
	}
}

static void
gbe_cat_buf(u8 *b)
{
	if (rw_file_exact(STDOUT_FILENO, b,
	    GBE_PART_SIZE, 0, IO_WRITE, LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY) < 0)
		err(errno, "stdout: cat");
}

static void
write_gbe_file(void)
{
	struct stat gbe_st;

	size_t p;
	size_t partnum;
	u8 update_checksum;

	if (command[cmd_index].flags == O_RDONLY)
		return;

	update_checksum = command[cmd_index].chksum_write;

	override_part_modified();
	
	if (fstat(gbe_fd, &gbe_st) == -1)
		err(errno, "%s: re-check", fname);

	if (gbe_st.st_size != gbe_file_size)
		err(errno, "%s: file size changed before write", fname);

	if (!S_ISREG(gbe_st.st_mode))
		err(errno, "%s: file type changed before write", fname);

	for (p = 0; p < 2; p++) {
		partnum = p ^ command[cmd_index].invert;

		if (!part_modified[partnum])
			continue;

		if (update_checksum)
			set_checksum(partnum);

		rw_gbe_file_part(partnum, IO_PWRITE, "pwrite");
	}
}

static void
override_part_modified(void)
{
	u8 mod_type = command[cmd_index].set_modified;

	switch (mod_type) {
	case SET_MOD_0:
		set_part_modified(0);
		break;
	case SET_MOD_1:
		set_part_modified(1);
		break;
	case SET_MOD_N:
		set_part_modified(part ^ command[cmd_index].invert);
		break;
	case SET_MOD_BOTH:
		set_part_modified(0);
		set_part_modified(1);
		break;
	case SET_MOD_OFF:
		break;
	default:
		err(EINVAL, "Unsupported set_mod type: %u",
		    mod_type);
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
	u8 invert = command[cmd_index].invert;

	u8 *mem_offset;
	off_t file_offset;

	if (rw_type < IO_PREAD || rw_type > IO_PWRITE)
		err(errno, "%s: %s: part %lu: invalid rw_type, %d",
		    fname, rw_type_str, (ulong)p, rw_type);

	if (rw_type == IO_PWRITE)
		invert = 0;

	/*
	 * Inverted reads are used by copy/swap.
	 * E.g. read from p0 (file) to p1 (mem).
	 */
	mem_offset = gbe_mem_offset(p ^ invert, rw_type_str);
	file_offset = (off_t)gbe_file_offset(p, rw_type_str);

	r = rw_gbe_file_exact(gbe_fd, mem_offset,
	    gbe_rw_size, file_offset, rw_type);

	if (r == -1)
		err(errno, "%s: %s: part %lu",
		    fname, rw_type_str, (ulong)p);

	if ((size_t)r != gbe_rw_size)
		err(EIO, "%s: partial %s: part %lu",
		    fname, rw_type_str, (ulong)p);
}

static void
check_written_part(size_t p)
{
	ssize_t r;
	size_t gbe_rw_size;
	u8 *mem_offset;
	off_t file_offset;
	u8 *buf_restore;

	if (!part_modified[p])
		return;

	gbe_rw_size = command[cmd_index].rw_size;

	/* invert not needed for pwrite */
	mem_offset = gbe_mem_offset(p, "pwrite");
	file_offset = (off_t)gbe_file_offset(p, "pwrite");

	memset(pad, 0xff, sizeof(pad));

	r = rw_gbe_file_exact(gbe_fd, pad,
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

/*
 * This one is similar to gbe_file_offset,
 * but used to check Gbe bounds in memory,
 * and it is *also* used during file I/O.
 */
static u8 *
gbe_mem_offset(size_t p, const char *f_op)
{
	off_t gbe_off = gbe_x_offset(p, f_op, "mem",
	    GBE_PART_SIZE, GBE_FILE_SIZE);

	return (u8 *)(buf + gbe_off);
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
	if (mem == NULL)
		goto err_rw_gbe_file_exact;

	if (mem != (void *)pad
	    && mem != (void *)rnum
	    && (mem < buf || mem >= (buf + GBE_FILE_SIZE)))
		goto err_rw_gbe_file_exact;

	if (off < 0 || off >= gbe_file_size)
		goto err_rw_gbe_file_exact;

	if (nrw > (size_t)(gbe_file_size - off))
		goto err_rw_gbe_file_exact;

	if (nrw > GBE_PART_SIZE)
		goto err_rw_gbe_file_exact;

	return rw_file_exact(fd, mem, nrw, off, rw_type,
	    NO_LOOP_EAGAIN, LOOP_EINTR, MAX_ZERO_RW_RETRY);

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
 * It calls rw_file_once(), which will
 * re-try on zero-read a finite number
 * of times, to prevent infinite loops
 * while also having fault tolerance.
 */
static ssize_t
rw_file_exact(int fd, u8 *mem, size_t nrw,
    off_t off, int rw_type, int loop_eagain,
    int loop_eintr, size_t max_retries)
{
	ssize_t rv;
	size_t rc;

	for (rc = 0, rv = 0; rc < nrw; ) {
		if ((rv = rw_file_once(fd, mem, nrw, off, rw_type, rc,
		    loop_eagain, loop_eintr, max_retries)) < 0)
			return -1;
	
		/* rw_file_once never returns
		   zero, but it's still logically
		   incorrect not to handle it here */
	
		if (rv == 0) {
			errno = EIO;
			return -1;
		}

		/* Prevent theoretical overflow */
		if ((size_t)rv > nrw - rc)
			goto err_rw_file_exact;

		rc += (size_t)rv;
	}

	return rc;

err_rw_file_exact:
	errno = EIO;
	return -1;
}

/*
 * rw_file_once() - Read less than perfectly
 *                  (and possibly die)
 *
 * Read/write, but don't insist on an
 * absolute read; e.g. if 100 bytes are
 * requested, this may return 80 <-- fine
 *
 * This function will never return zero.
 * It will only return below (error),
 * or above (success). On error, -1 is
 * returned and errno is set accordingly.
 *
 * Zero-byte returns are not allowed.
 */
static ssize_t
rw_file_once(int fd, u8 *mem, size_t nrw,
    off_t off, int rw_type, size_t rc,
    int loop_eagain, int loop_eintr,
    size_t max_retries)
{
	ssize_t rv;
	size_t retries_on_zero = 0;

	if (mem == NULL)
		goto err_rw_file_once;

read_again:
	rv = prw(fd, mem + rc, nrw - rc, off + rc, rw_type,
	    loop_eagain, loop_eintr);

	if (rv < 0)
		return -1;

	if ((size_t)rv > (nrw - rc))/* don't overflow */
		goto err_rw_file_once;

	if (rv != 0)
		return rv;

	if (retries_on_zero++ < max_retries)
		goto read_again;

err_rw_file_once:
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
 */

static ssize_t
prw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type,
    int loop_eagain, int loop_eintr)
{
	off_t off_orig;
	off_t off_last;
	ssize_t r;
	int saved_errno;
	int positional_rw;
	struct stat st;

	if (mem == NULL)
		goto err_prw;

	if (fd < 0
	    || off < 0
	    || !nrw /* prevent zero read request */
	    || nrw > (size_t)SSIZE_MAX /* prevent overflow */
	    || (uint)rw_type > IO_PWRITE)
		goto err_prw;

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
	    loop_eagain, loop_eintr)) == (off_t)-1)
		r = -1;
	else if (lseek_loop(fd, off, SEEK_SET,
	    loop_eagain, loop_eintr) == (off_t)-1)
		r = -1;

	do {
		if (rw_type == IO_PREAD)
			r = read(fd, mem, nrw);
		else if (rw_type == IO_PWRITE)
			r = write(fd, mem, nrw);

		r = rw_over_nrw(r, nrw);
	} while (r == -1 &&
	    (errno == try_err(loop_eintr, EINTR)
	    || errno == try_err(loop_eagain, EAGAIN)));

	saved_errno = errno;
	off_last = lseek_loop(fd, off_orig, SEEK_SET,
	    loop_eagain, loop_eintr);
	if (off_last == (off_t)-1) {
		errno = saved_errno;
		return -1;
	}
	if (off_last != off_orig)
		goto err_prw;
	errno = saved_errno;

	return rw_over_nrw(r, nrw);
#endif

err_prw:
	errno = EIO;
	return -1;
}

static int
check_file(int fd, struct stat *st)
{
	if (fstat(fd, st) == -1)
		goto err_is_file;

	if (S_ISREG(st->st_mode))
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
static int
rw_over_nrw(ssize_t r, size_t nrw)
{
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

static int
close_files(void)
{
	int close_err_gbe = 0;
	int close_err_rand = 0;
	int saved_errno = errno;

	if (gbe_fd > -1) {
		if (close(gbe_fd) == -1)
			close_err_gbe = errno;
		gbe_fd = -1;
	}

	if (urandom_fd > -1) {
		if (close(urandom_fd) == -1)
			close_err_rand = errno;
		urandom_fd = -1;
	}

	if (saved_errno)
		errno = saved_errno;

	if (close_err_gbe || close_err_rand)
		return -1;

	return 0;
}

static void
err(int nvm_errval, const char *msg, ...)
{
	va_list args;

	if (errno <= 0)
		errno = ECANCELED;
	if (!errno)
		errno = nvm_errval;

	(void)close_files();

	fprintf(stderr, "%s: ", getnvmprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	fprintf(stderr, ": %s", strerror(errno));

	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
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

static void
usage(int usage_exit)
{
	const char *util = getnvmprogname();

#ifdef NVMUTIL_PLEDGE
	if (pledge("stdio", NULL) == -1)
		err(errno, "pledge");
#endif
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

	if (usage_exit)
		err(EINVAL, "Too few arguments");
}
