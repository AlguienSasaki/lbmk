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
 * -Os -Wall -Wextra -Werror -pedantic -std=c99
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifdef __OpenBSD__
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#if defined(__has_include)
#if __has_include(<stdint.h>)
#include <stdint.h>
#else
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
#endif
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#include <stdint.h>
#else
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef char static_assert_char_is_8_bits[(CHAR_BIT == 8) ? 1 : -1];
typedef char static_assert_uint8_is_1[(sizeof(uint8_t) == 1) ? 1 : -1];
typedef char static_assert_uint16_is_2[(sizeof(uint16_t) == 2) ? 1 : -1];
typedef char static_assert_uint32_is_4[(sizeof(uint32_t) == 4) ? 1 : -1];
typedef char static_assert_int_ge_32[(sizeof(int) >= 4) ? 1 : -1];
typedef char static_assert_twos_complement[
    ((-1 & 3) == 3) ? 1 : -1
];

/*
 * We set _FILE_OFFSET_BITS 64, but we only handle
 * files that are 128KB in size at a maximum, so we
 * realistically only need 32-bit at a minimum.
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

#ifndef O_BINARY
#define O_BINARY 0
#endif

/*
 * Sanitize command tables.
 */
static void sanitize_command_list(void);
static void sanitize_command_index(size_t c);
static void check_enum_bin(size_t a, const char *a_name,
    size_t b, const char *b_name);

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
static uint8_t valid_command(size_t c);

/*
 * Helper functions for command: setmac
 */
static void cmd_helper_setmac(void);
static void parse_mac_string(void);
static size_t xstrxlen(const char *scmp, size_t maxlen);
static void set_mac_byte(size_t mac_byte_pos);
static void set_mac_nib(size_t mac_str_pos,
    size_t mac_byte_pos, size_t mac_nib_pos);
static uint16_t hextonum(char ch_s);
static uint16_t rhex(void);
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
static void gbe_cat_buf(uint8_t *b);

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
static uint16_t calculated_checksum(size_t p);

/*
 * Helper functions for accessing
 * the NVM area during operation.
 */
static uint16_t nvm_word(size_t pos16, size_t part);
static void set_nvm_word(size_t pos16, size_t part, uint16_t val16);
static void set_part_modified(size_t p);
static void check_nvm_bound(size_t pos16, size_t part);
static void check_bin(size_t a, const char *a_name);

/*
 * Helper functions for stub functions
 * that handle GbE file reads/writes.
 */
static void rw_gbe_file_part(size_t p, int rw_type,
    const char *rw_type_str);
static uint8_t *gbe_mem_offset(size_t part, const char *f_op);
static off_t gbe_file_offset(size_t part, const char *f_op);
static off_t gbe_x_offset(size_t part, const char *f_op,
    const char *d_type, off_t nsize, off_t ncmp);
static void rw_file_exact(int fd, uint8_t *mem, size_t len,
    off_t off, int rw_type, const char *path, const char *rw_type_str);
static ssize_t prw(int fd, void *mem, size_t count,
    off_t offset, int rw_type, const char *path);
static off_t lseek_eintr(int fd, off_t offset, int whence);

/*
 * Error handling and cleanup
 */
static void err(int nvm_errval, const char *msg, ...);
static void close_files(void);
static const char *getnvmprogname(void);
static void set_err(int errval);
static void usage(uint8_t usage_exit);

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

/*
 * Portable macro based on BSD nitems.
 * Used to count the number of commands (see below).
 */
#define items(x) (sizeof((x)) / sizeof((x)[0]))

static const char newrandom[] = "/dev/urandom";
static const char oldrandom[] = "/dev/random"; /* fallback on OLD unix */
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
static uint8_t buf[GBE_FILE_SIZE];
static uint8_t pad[GBE_PART_SIZE]; /* the file that wouldn't die */

static uint16_t mac_buf[3];
static off_t gbe_file_size;

static int urandom_fd = -1;
static int gbe_fd = -1;
static size_t part;
static uint8_t part_modified[2];
static uint8_t part_valid[2];

static const char rmac[] = "xx:xx:xx:xx:xx:xx";
static const char *mac_str;
static const char *fname;
static const char *argv0;

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
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

enum {
	LESEN,
	PLESEN,
	SCHREIB,
	PSCHREIB
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
	uint8_t invert;
	uint8_t set_modified;
	uint8_t arg_part;
	uint8_t chksum_read;
	uint8_t chksum_write;
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
		err(ECANCELED, "pledge");
	if (unveil("/dev/null", "r") == -1)
		err(ECANCELED, "unveil '/dev/null'");
#else
	if (pledge("stdio rpath wpath", NULL) == -1)
		err(ECANCELED, "pledge");
#endif
#endif

	sanitize_command_list();

	set_cmd(argc, argv);
	set_cmd_args(argc, argv);

#ifdef NVMUTIL_PLEDGE
#ifdef NVMUTIL_UNVEIL
	if (command[cmd_index].flags == O_RDONLY) {
		if (unveil(fname, "r") == -1)
			err(ECANCELED, "%s: unveil ro", fname);
		if (unveil(NULL, NULL) == -1)
			err(ECANCELED, "unveil block (ro)");
		if (pledge("stdio rpath", NULL) == -1)
			err(ECANCELED, "pledge ro (kill unveil)");
	} else {
		if (unveil(fname, "rw") == -1)
			err(ECANCELED, "%s: unveil rw", fname);
		if (unveil(NULL, NULL) == -1)
			err(ECANCELED, "unveil block (rw)");
		if (pledge("stdio rpath wpath", NULL) == -1)
			err(ECANCELED, "pledge rw (kill unveil)");
	}
#else
	if (command[cmd_index].flags == O_RDONLY) {
		if (pledge("stdio rpath", NULL) == -1)
			err(ECANCELED, "pledge ro");
	}
#endif
#endif

	open_dev_urandom();

	open_gbe_file();

#ifdef NVMUTIL_PLEDGE
	if (pledge("stdio", NULL) == -1)
		err(ECANCELED, "pledge stdio (main)");
#endif

	/*
	 * Used by CMD_CAT, for padding
	 */
	memset(pad, 0xff, sizeof(pad));

	read_gbe_file();
	read_checksums();

	errno = 0;
	run_cmd(cmd_index);

	if (errno)
		err(errno, "%s: Unhandled error (WRITE SKIPPED)", fname);
	else if (command[cmd_index].flags == O_RDWR)
		write_gbe_file();

	close_files();

	if (errno)
		err(ECANCELED, "Unhandled error on exit");

	return EXIT_SUCCESS;
}

/*
 * Guard against regressions by maintainers (command table)
 */
static void
sanitize_command_list(void)
{
	size_t c;

	for (c = 0; valid_command(c); c++)
		sanitize_command_index(c);
}

static void
sanitize_command_index(size_t c)
{
	uint8_t mod_type;
	size_t gbe_rw_size;

	check_command_num(c);

	if (ARGC_3 != 3)
		err(ECANCELED, "ARGC_3 is not equal to 3");
	if (ARGC_4 != 4)
		err(ECANCELED, "ARGC_4 is not equal to 4");

	if (command[c].argc < 3)
		err(ECANCELED, "cmd index %lu: argc below 3, %d",
		    c, command[c].argc);

	if (command[c].str == NULL)
		err(ECANCELED, "cmd index %lu: NULL str", c);
	if (*command[c].str == '\0')
		err(ECANCELED, "cmd index %lu: empty str", c);

	if (xstrxlen(command[c].str, MAX_CMD_LEN + 1) >
	    MAX_CMD_LEN) {
		err(ECANCELED, "cmd index %lu: str too long: %s",
		    c, command[c].str);
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

	check_enum_bin(ARG_NOPART, "ARG_NOPART", ARG_PART, "ARG_PART");
	check_enum_bin(SKIP_CHECKSUM_READ, "SKIP_CHECKSUM_READ",
	    CHECKSUM_READ, "CHECKSUM_READ");
	check_enum_bin(SKIP_CHECKSUM_WRITE, "SKIP_CHECKSUM_WRITE",
	    CHECKSUM_WRITE, "CHECKSUM_WRITE");

	gbe_rw_size = command[c].rw_size;

	switch (gbe_rw_size) {
	case GBE_PART_SIZE:
	case NVM_SIZE:
		break;
	default:
		err(EINVAL, "Unsupported rw_size: %lu", gbe_rw_size);
	}

	if (gbe_rw_size > GBE_PART_SIZE)
		err(EINVAL, "rw_size larger than GbE part: %lu",
		    gbe_rw_size);

	if (command[c].flags != O_RDONLY &&
	    command[c].flags != O_RDWR)
		err(EINVAL, "invalid cmd.flags setting");

	if (!((PLESEN > LESEN) && (SCHREIB > PLESEN) && (PSCHREIB > SCHREIB)))
		err(EINVAL, "some rw type integers are the same");
}

static void
check_enum_bin(size_t a, const char *a_name,
    size_t b, const char *b_name)
{
	if (a)
		err(ECANCELED, "%s is non-zero", a_name);

	if (b != 1)
		err(ECANCELED, "%s is a value other than 1", b_name);
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
	uint8_t arg_part;

	if (!valid_command(cmd_index) || argc < 3)
		usage(1);

	arg_part = command[cmd_index].arg_part;

	/* Maintainer bugs */
	if (arg_part && argc < 4)
		err(ECANCELED,
		    "arg_part set for command that needs argc4");
	if (arg_part && cmd_index == CMD_SETMAC)
		err(ECANCELED,
		    "arg_part set on CMD_SETMAC");

	if (cmd_index == CMD_SETMAC)
		mac_str = argc >= 4 ? argv[3] : rmac;
	else if (arg_part)
		part = conv_argv_part_num(argv[3]);
}

static size_t
conv_argv_part_num(const char *part_str)
{
	unsigned char ch;

	if (part_str[0] == '\0' || part_str[1] != '\0')
		err(EINVAL, "Partnum string '%s' wrong length", part_str);

	/* char signedness is implementation-defined */
	ch = (unsigned char)part_str[0];
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
			return (unsigned char)a[i] - (unsigned char)b[i];

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
	struct stat st_urandom_fd;

	rname = newrandom;
	if ((urandom_fd = open(rname, O_RDONLY | O_BINARY)) != -1)
		return;

	/*
	 * Fall back to /dev/random on very old Unix.
	 */

	fprintf(stderr, "Can't open %s (will use %s instead)\n",
	    newrandom, oldrandom);

	errno = 0;

	rname = oldrandom;
	xopen(&urandom_fd, rname, O_RDONLY | O_BINARY, &st_urandom_fd);
}

static void
open_gbe_file(void)
{
	struct stat gbe_st;

	xopen(&gbe_fd, fname, command[cmd_index].flags | O_BINARY, &gbe_st);

	gbe_file_size = gbe_st.st_size;

	switch (gbe_file_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		break;
	default:
		err(ECANCELED, "File size must be 8KB, 16KB or 128KB");
	}
}

static void
xopen(int *fd_ptr, const char *path, int flags, struct stat *st)
{
	if ((*fd_ptr = open(path, flags)) == -1)
		err(ECANCELED, "%s", path);

	if (fstat(*fd_ptr, st) == -1)
		err(ECANCELED, "%s", path);

	if (!S_ISREG(st->st_mode))
		err(ECANCELED, "%s: not a regular file", path);
}

static void
read_gbe_file(void)
{
	size_t p;
	uint8_t do_read[2] = {1, 1};

	/*
	 * Commands specifying a partnum only
	 * need the given GbE part to be read.
	 */
	if (command[cmd_index].arg_part)
		do_read[part ^ 1] = 0;

	for (p = 0; p < 2; p++) {
		if (do_read[p])
			rw_gbe_file_part(p, PLESEN, "pread");
	}
}

static void
read_checksums(void)
{
	size_t p;
	size_t skip_part;
	uint8_t invert;
	uint8_t arg_part;
	uint8_t num_invalid;
	uint8_t max_invalid;

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

		if (good_checksum(p))
			part_valid[p] = 1;
		else
			++num_invalid;
	}

	if (num_invalid < max_invalid)
		errno = 0;

	if (num_invalid >= max_invalid) {
		if (max_invalid == 1)
			err(ECANCELED, "%s: part %lu has a bad checksum",
			    fname, part);
		err(ECANCELED, "%s: No valid checksum found in file",
		    fname);
	}
}

static int
good_checksum(size_t partnum)
{
	uint16_t expected_checksum = calculated_checksum(partnum);
	uint16_t current_checksum = nvm_word(NVM_CHECKSUM_WORD, partnum);

	if (current_checksum == expected_checksum)
		return 1;

	set_err(ECANCELED);
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
		err(ECANCELED, "Invalid run_cmd arg: %lu", c);
}

static uint8_t
valid_command(size_t c)
{
	if (c >= N_COMMANDS)
		return 0;

	if (c != command[c].chk)
		err(ECANCELED, "Invalid cmd chk value (%lu) vs arg: %lu",
		    command[c].chk, c);

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
	uint16_t hex_num;

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

static uint16_t
hextonum(char ch_s)
{
	unsigned char ch = (unsigned char)ch_s;

	if ((unsigned)(ch - '0') <= 9)
		return ch - '0';

	ch |= 0x20;

	if ((unsigned)(ch - 'a') <= 5)
		return ch - 'a' + 10;

	if (ch == '?' || ch == 'x')
		return rhex(); /* random character */

	return 16; /* invalid character */
}

static uint16_t
rhex(void)
{
	static size_t n = 0;
	static uint8_t rnum[12];

	if (!n) {
		n = sizeof(rnum);
		rw_file_exact(urandom_fd, rnum, n, 0, LESEN, rname, "read");
	}

	return (uint16_t)(rnum[--n] & 0xf);
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

	printf("Wrote MAC address to part %lu: ", partnum);
	print_mac_from_nvm(partnum);
}

static void
cmd_helper_dump(void)
{
	size_t partnum;

	part_valid[0] = good_checksum(0);
	part_valid[1] = good_checksum(1);

	if (part_valid[0] || part_valid[1])
		errno = 0;

	for (partnum = 0; partnum < 2; partnum++) {
		if (!part_valid[partnum])
			fprintf(stderr,
			    "BAD checksum %04x in part %lu (expected %04x)\n",
			    nvm_word(NVM_CHECKSUM_WORD, partnum),
			    partnum, calculated_checksum(partnum));

		printf("MAC (part %lu): ", partnum);
		print_mac_from_nvm(partnum);
		hexdump(partnum);
	}
}

static void
print_mac_from_nvm(size_t partnum)
{
	size_t c;

	for (c = 0; c < 3; c++) {
		uint16_t val16 = nvm_word(c, partnum);
		printf("%02x:%02x", val16 & 0xff, val16 >> 8);
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
	uint16_t val16;

	for (row = 0; row < 8; row++) {
		printf("%08lx ", (size_t)row << 4);
		for (c = 0; c < 8; c++) {
			val16 = nvm_word((row << 3) + c, partnum);
			if (c == 4)
				printf(" ");
			printf(" %02x %02x", val16 & 0xff, val16 >> 8);
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
		err(ECANCELED, "cmd_helper_cat called erroneously");

	fflush(NULL);

	for (p = 0; p < 2; p++) {
		gbe_cat_buf(buf + (p * GBE_PART_SIZE));

		for (ff = 0; ff < n; ff++)
			gbe_cat_buf(pad);
	}
}

static void
gbe_cat_buf(uint8_t *b)
{
	rw_file_exact(STDOUT_FILENO, b, GBE_PART_SIZE, 0,
	    SCHREIB, "stdout", "write");
}

static void
write_gbe_file(void)
{
	size_t p;
	size_t partnum;
	uint8_t update_checksum;

	if (command[cmd_index].flags == O_RDONLY)
		return;

	update_checksum = command[cmd_index].chksum_write;

	override_part_modified();

	for (p = 0; p < 2; p++) {
		partnum = p ^ command[cmd_index].invert;

		if (!part_modified[partnum])
			continue;

		if (update_checksum)
			set_checksum(partnum);

		rw_gbe_file_part(partnum, PSCHREIB, "pwrite");
	}
}

static void
override_part_modified(void)
{
	uint8_t mod_type = command[cmd_index].set_modified;

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

static uint16_t
calculated_checksum(size_t p)
{
	size_t c;
	uint32_t val16 = 0;

	for (c = 0; c < NVM_CHECKSUM_WORD; c++)
		val16 += (uint32_t)nvm_word(c, p);

	return (uint16_t)((NVM_CHECKSUM - val16) & 0xffff);
}

/*
 * GbE NVM files store 16-bit (2-byte) little-endian words.
 * We must therefore swap the order when reading or writing.
 *
 * NOTE: The MAC address words are stored big-endian in the
 * file, but we assume otherwise and adapt accordingly.
 */

static uint16_t
nvm_word(size_t pos16, size_t p)
{
	size_t pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	return (uint16_t)buf[pos] |
	    ((uint16_t)buf[pos + 1] << 8);
}

static void
set_nvm_word(size_t pos16, size_t p, uint16_t val16)
{
	size_t pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	buf[pos] = (uint8_t)(val16 & 0xff);
	buf[pos + 1] = (uint8_t)(val16 >> 8);

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
		err(EINVAL, "check_nvm_bound: out of bounds %lu", c);
}

static void
check_bin(size_t a, const char *a_name)
{
	if (a > 1)
		err(ECANCELED, "%s must be 0 or 1, but is %lu", a_name, a);
}

static void
rw_gbe_file_part(size_t p, int rw_type,
    const char *rw_type_str)
{
	size_t gbe_rw_size = command[cmd_index].rw_size;
	uint8_t invert = command[cmd_index].invert;

	uint8_t *mem_offset;

	if (rw_type == SCHREIB || rw_type == PSCHREIB)
		invert = 0;

	/*
	 * Inverted reads are used by copy/swap.
	 * E.g. read from p0 (file) to p1 (mem).
	 */
	mem_offset = gbe_mem_offset(p ^ invert, rw_type_str);

	rw_file_exact(gbe_fd, mem_offset,
	    gbe_rw_size, gbe_file_offset(p, rw_type_str),
	    rw_type, fname, rw_type_str);
}

/*
 * This one is similar to gbe_file_offset,
 * but used to check Gbe bounds in memory,
 * and it is *also* used during file I/O.
 */
static uint8_t *
gbe_mem_offset(size_t p, const char *f_op)
{
	off_t gbe_off = gbe_x_offset(p, f_op, "mem",
	    GBE_PART_SIZE, GBE_FILE_SIZE);

	return (uint8_t *)(buf + gbe_off);
}

/*
 * Reads to GbE from write_gbe_file_part and read_gbe_file_part
 * are filtered through here. These operations must
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

	if (off + GBE_PART_SIZE > ncmp)
		err(ECANCELED, "%s: GbE %s %s out of bounds",
		    fname, d_type, f_op);

	if (off != 0 && off != ncmp >> 1)
		err(ECANCELED, "%s: GbE %s %s at bad offset",
		    fname, d_type, f_op);

	return off;
}

static void
rw_file_exact(int fd, uint8_t *mem, size_t len,
    off_t off, int rw_type, const char *path,
    const char *rw_type_str)
{
	ssize_t rval = 0;
	size_t rc = 0;

	if (fd < 0)
		err(EIO, "%s: %s: Bad fd %d", path, rw_type_str, fd);
	if (!len)
		err(EIO, "%s: %s: Zero length", path, rw_type_str);
	if (len > (size_t)SSIZE_MAX)
		err(EIO,
		    "%s: %s: Requested length (%lu) exceeds SSIZE_MAX (%zd)",
		    path, rw_type_str, len, SSIZE_MAX);

	while (rc < len) {
		if (rw_type == PSCHREIB)
			rval = prw(fd, mem + rc, len - rc,
			    off + rc, rw_type, path);
		else if (rw_type == SCHREIB)
			rval = write(fd, mem + rc, len - rc);
		else if (rw_type == PLESEN)
			rval = prw(fd, mem + rc, len - rc,
			    off + rc, rw_type, path);
		else if (rw_type == LESEN)
			rval = read(fd, mem + rc, len - rc);
		else
			err(EIO, "%s: %s: Unsupported rw_type",
			    path, rw_type_str);

		if (rval >= 0) {
			if (rval == 0)
				err(EIO, "%s: %s: 0-byte return",
				    path, rw_type_str);

			if ((size_t)rval > (len - rc))
				err(EIO, "%s: %s: Buffer overread trap",
				    path, rw_type_str);

			rc += (size_t)rval;
			continue;
		}

		if (errno != EINTR)
			err(EIO, "%s: %s", path, rw_type_str);

		errno = 0;
	}
}

static ssize_t
prw(int fd, void *mem, size_t count,
    off_t offset, int rw_type, const char *path)
{
	off_t old;
	ssize_t r;
	int saved_errno = 0;

	if ((old = lseek_eintr(fd, (off_t)0, SEEK_CUR)) == (off_t)-1)
		return -1;

	if (lseek_eintr(fd, offset, SEEK_SET) == (off_t)-1)
		return -1;

	do {
		if (rw_type == PLESEN)
			r = read(fd, mem, count);
		else if (rw_type == PSCHREIB)
			r = write(fd, mem, count);
		else
			err(EIO, "%s: Invalid rw_type", path);
	} while (r < 0 && errno == EINTR);

	if (r < 0)
		saved_errno = errno;

	if (lseek_eintr(fd, old, SEEK_SET) == (off_t)-1)
		return -1;

	if (r < 0)
		errno = saved_errno;

	return r;
}

static off_t
lseek_eintr(int fd, off_t offset, int whence)
{
	off_t old;

	do {
		old = lseek(fd, offset, whence);
	} while (old == (off_t)-1 && errno == EINTR);

	return old;
}

static void
err(int nvm_errval, const char *msg, ...)
{
	va_list args;

	/*
	 * We need to ensure that files are closed
	 * on exit, including error exits. This
	 * would otherwise recurse, because the
	 * close_files() function also calls err(),
	 * but with -1 on nvm_errval. It's the only
	 * one that does this.
	 *
	 * Since the errval is for setting errno, -1
	 * would be incorrect. Therefore, set_err()
	 * avoids overriding errno if the given value
	 * is negative.
	 *
	 * Be careful modifying err() and close_files().
	 */
	if (nvm_errval != -1)
		close_files();

	fprintf(stderr, "%s: ", getnvmprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	set_err(nvm_errval);
	fprintf(stderr, ": %s", strerror(errno));

	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

static void
close_files(void)
{
	if (gbe_fd > -1) {
		if (close(gbe_fd) == -1)
			err(-1, "%s: close failed", fname);
		gbe_fd = -1;
	}

	if (urandom_fd > -1) {
		if (close(urandom_fd) == -1)
			err(-1, "%s: close failed", rname);
		urandom_fd = -1;
	}
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
set_err(int x)
{
	if (errno)
		return;
	if (x > 0)
		errno = x;
	else
		errno = ECANCELED;
}

static void
usage(uint8_t usage_exit)
{
	const char *util = getnvmprogname();

#ifdef NVMUTIL_PLEDGE
	if (pledge("stdio", NULL) == -1)
		err(ECANCELED, "pledge");
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
