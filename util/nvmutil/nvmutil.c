/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org> */
/* Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com> */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * On the platforms below, we will use arc4random
 * for random MAC address generation.
 *
 * Later on, the code has fallbacks for other systems.
 */
#if defined(__OpenBSD__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__APPLE__) || \
    defined(__DragonFly__)
#ifndef HAVE_ARC4RANDOM_BUF
#define HAVE_ARC4RANDOM_BUF
#endif
#endif

static void set_cmd(int argc, char *argv[]);
static void check_cmd_args(int argc, char *argv[]);
static size_t conv_argv_part_num(const char *part_str);
static void run_cmd(ssize_t c);
static void set_io_flags(int argc, char *argv[]);
static void open_gbe_file(void);
#ifndef HAVE_ARC4RANDOM_BUF
static void open_dev_urandom(void);
#endif
static void xopen(int *fd, const char *path, int flags, struct stat *st);
static void read_gbe_file(void);
static void read_gbe_file_part(size_t part);
static void cmd_setmac(void);
static void parse_mac_string(void);
static void set_mac_byte(size_t mac_byte_pos);
static void set_mac_nib(size_t mac_str_pos,
    size_t mac_byte_pos, size_t mac_nib_pos);
static uint16_t hextonum(char ch_s);
static uint16_t rhex(void);
static void read_file_exact(int fd, void *buf, size_t len,
    off_t off, const char *path, const char *op);
static int write_mac_part(size_t partnum);
static void cmd_dump(void);
static void print_mac_address(size_t partnum);
static void hexdump(size_t partnum);
static void cmd_setchecksum(void);
static void set_checksum(size_t part);
static void cmd_brick(void);
static void cmd_copy(void);
static void cmd_swap(void);
static int good_checksum(size_t partnum);
static uint16_t word(size_t pos16, size_t part);
static void set_word(size_t pos16, size_t part, uint16_t val16);
static void check_nvm_bound(size_t pos16, size_t part);
static void write_gbe_file(void);
static void write_gbe_file_part(size_t part);
static off_t gbe_file_offset(size_t part, const char *f_op);
static void *gbe_mem_offset(size_t part, const char *f_op);
static off_t gbe_x_offset(size_t part, const char *f_op,
    const char *d_type, off_t nsize, off_t ncmp);
static void set_part_modified(size_t p);
static void check_part_num(size_t p);
static void usage(void);
static void err(int nvm_errval, const char *msg, ...);
static const char *getnvmprogname(void);
static void set_err(int errval);

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
 * When reading files, we loop on error EINTR
 * a maximum number of times as defined, thus:
 */
#define MAX_RETRY_READ 30

/*
 * Portable macro based on BSD nitems.
 * Used to count the number of commands (see below).
 */
#define items(x) (sizeof((x)) / sizeof((x)[0]))

#ifndef HAVE_ARC4RANDOM_BUF
static const char newrandom[] = "/dev/urandom";
static const char oldrandom[] = "/dev/random"; /* fallback on OLD unix */
static const char *rname = NULL;
#endif

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

static uint16_t mac_buf[3];
static off_t gbe_file_size;

static int gbe_flags;
#ifndef HAVE_ARC4RANDOM_BUF
static int urandom_fd = -1;
#endif
static int gbe_fd = -1;
static size_t part;
static uint8_t part_modified[2];

static const char *mac_str;
static const char rmac[] = "xx:xx:xx:xx:xx:xx";
static const char *fname;
static const char *argv0;

#define PART_INVERT 1
#define NO_INVERT 0
#define ARGC_3 3
#define ARGC_4 4

/*
 * Used as indices for command[]
 *
 * MUST be in the same order as entries in
 * command[] - or run_cmd() will detect this,
 * and cause a non-zero exit (err).
 */
enum {
	CMD_DUMP,
	CMD_SETMAC,
	CMD_SWAP,
	CMD_COPY,
	CMD_BRICK,
	CMD_SETCHECKSUM
};
#define CMD_NULL -1

struct commands {
	size_t chk; /* use by in later check on run_cmd,
		against cmd index, to verify correct enum order */
	const char *str;
	void (*run)(void);
	int argc;
	uint8_t invert;
};

/*
 * Pointers used for running nvmutil commands
 */
static const struct commands command[] = {
	{ CMD_DUMP, "dump", cmd_dump, ARGC_3, NO_INVERT },
	{ CMD_SETMAC, "setmac", cmd_setmac, ARGC_3, NO_INVERT },
	{ CMD_SWAP, "swap", cmd_swap, ARGC_3, PART_INVERT },
	{ CMD_COPY, "copy", cmd_copy, ARGC_4, PART_INVERT },
	{ CMD_BRICK, "brick", cmd_brick, ARGC_4, NO_INVERT },
	{ CMD_SETCHECKSUM, "setchecksum", cmd_setchecksum,
	    ARGC_4, NO_INVERT },
};

/*
 * Index in command[], will be set later
 */
static ssize_t cmd = CMD_NULL;

int
main(int argc, char *argv[])
{
	argv0 = argv[0];
	if (argc < 2)
		usage();

	fname = argv[1];

#ifdef __OpenBSD__
	if (pledge("stdio rpath wpath unveil", NULL) == -1)
		err(ECANCELED, "pledge");

	/*
	 * For restricted filesystem access on early error.
	 *
	 * Unveiling the random device early, regardless of
	 * whether we will use it, prevents operations on any
	 * GbE files until we permit it, while performing the
	 * prerequisite error checks.
	 *
	 * We don't actually use the random device on platforms
	 * that have arc4random, which includes OpenBSD.
	 */
	if (unveil("/dev/urandom", "r") == -1)
		err(ECANCELED, "unveil '/dev/urandom'");
	if (unveil("/dev/random", "r") == -1)
		err(ECANCELED, "unveil '/dev/random'");
#endif

	set_cmd(argc, argv);
	check_cmd_args(argc, argv);
	set_io_flags(argc, argv);

#ifdef __OpenBSD__
	if (gbe_flags == O_RDONLY) {
		if (unveil(fname, "r") == -1)
			err(ECANCELED, "unveil ro '%s'", fname);
		if (unveil(NULL, NULL) == -1)
			err(ECANCELED, "unveil block (ro)");
		if (pledge("stdio rpath", NULL) == -1)
			err(ECANCELED, "pledge ro (kill unveil)");
	} else {
		if (unveil(fname, "rw") == -1)
			err(ECANCELED, "unveil rw '%s'", fname);
		if (unveil(NULL, NULL) == -1)
			err(ECANCELED, "unveil block (rw)");
		if (pledge("stdio rpath wpath", NULL) == -1)
			err(ECANCELED, "pledge rw (kill unveil)");
	}
#endif

#ifdef HAVE_ARC4RANDOM_BUF
	if (cmd == CMD_SETMAC)
		printf("Randomisation method: arc4random_buf\n");
#else
	if (cmd == CMD_SETMAC)
		open_dev_urandom();
#endif
	open_gbe_file();

#ifdef __OpenBSD__
	if (pledge("stdio", NULL) == -1)
		err(ECANCELED, "pledge stdio (main)");
#endif

	read_gbe_file();
	run_cmd(cmd);
	write_gbe_file();

	if (close(gbe_fd) == -1)
		err(ECANCELED, "close '%s'", fname);
#ifndef HAVE_ARC4RANDOM_BUF
	if (close(urandom_fd) == -1)
		err(ECANCELED, "close '%s'", rname);
#endif

	/*
	 * We still exit with non-zero status if
	 * errno is set, but we don't need to print
	 * the error on dump commands, because they
	 * already print errors.
	 *
	 * If both parts have bad checksums, then
	 * cmd_dump will cause non-zero exit. If at
	 * least one part is valid, it resets errno.
	 *
	 * However, if we're not using cmd_dump, then
	 * we have a bug somewhere in the code.
	 */
	if (cmd != CMD_DUMP) {
		if (errno)
			err(ECANCELED, "Unhandled error on exit");
	}

	if (errno)
		return EXIT_FAILURE;
	else
		return EXIT_SUCCESS;
}

static void
set_cmd(int argc, char *argv[])
{
	/*
	 * No extra args: ./nvmutil gbe.bin
	 * Equivalent: ./nvmutil gbe.bin setmac xx:xx:xx:xx:xx:xx
	 */
	if (argc == 2) {
		cmd = CMD_SETMAC;
		return;
	}

	/*
	 * Three or more args.
	 * Example: ./nvmutil gbe.bin copy 0
	 */
	for (cmd = 0; cmd < (ssize_t)items(command); cmd++) {
		if (strcmp(argv[2], command[cmd].str) != 0)
			continue;
		if (argc >= command[cmd].argc) {
			return;
		}

		err(EINVAL, "Too few args: command '%s'", command[cmd].str);
	}

	cmd = CMD_NULL;
}

static void
check_cmd_args(int argc, char *argv[])
{
	if (cmd == CMD_NULL && argc > 2) {
		/*
		 * Example: ./nvmutil gbe.bin xx:1f:16:xx:xx:xx
		 * Equivalent ./nvmutil gbe.bin setmac xx:1f:16:xx:xx:xx
		 */
		mac_str = argv[2];
		cmd = CMD_SETMAC;
	} else if (cmd == CMD_SETMAC) {
		/*
		 * Example: ./nvmutil gbe.bin setmac xx:1f:16:xx:xx:xx
		 */
		mac_str = rmac; /* random MAC */
		if (argc > 3)
			mac_str = argv[3];
	} else if (cmd != CMD_NULL && argc > 3) { /* user-supplied partnum */
		/*
		 * Example: ./nvmutil gbe.bin copy 0
		 */
		part = conv_argv_part_num(argv[3]);
	}

	if (cmd == CMD_NULL)
		err(EINVAL, "Bad command");
}

static size_t
conv_argv_part_num(const char *part_str)
{
	unsigned char ch;

	/*
	 * Because char signedness is implementation-defined,
	 * we cast to unsigned char before arithmetic.
	 */

	if (part_str[0] == '\0' || part_str[1] != '\0')
		err(EINVAL, "Partnum string '%s' wrong length", part_str);

	ch = (unsigned char)part_str[0];

	if (ch < '0' || ch > '1')
		err(EINVAL, "Bad part number (%c)", ch);

	return (size_t)(ch - '0');
}

static void
run_cmd(ssize_t c)
{
	size_t d = (size_t)c;

	if (d >= items(command))
		err(ECANCELED, "run_cmd: Invalid run_cmd arg: %zd", c);

	if (d != command[d].chk)
		err(ECANCELED, "run_cmd: Invalid chk value (%zu) vs arg: %zd",
		    command[d].chk, c);

	command[d].run();
}

static void
set_io_flags(int argc, char *argv[])
{
	gbe_flags = O_RDWR;

	if (argc < 3)
		return;

	if (strcmp(argv[2], "dump") == 0)
		gbe_flags = O_RDONLY;
}

#ifndef HAVE_ARC4RANDOM_BUF
static void
open_dev_urandom(void)
{
	struct stat st_urandom_fd;

	printf("Randomisation method: %s\n", newrandom);

	/*
	 * Try /dev/urandom first
	 */
	rname = newrandom;
	if ((urandom_fd = open(rname, O_RDONLY)) != -1)
		return;

	fprintf(stderr, "Can't open %s (will use %s instead)\n",
	    newrandom, oldrandom);

	/*
	 * Fall back to /dev/random on old platforms
	 * where /dev/urandom does not exist.
	 *
	 * We must reset the error condition first,
	 * to prevent stale error status later.
	 */
	errno = 0;

	rname = oldrandom;
	xopen(&urandom_fd, rname, O_RDONLY, &st_urandom_fd);
}
#endif

static void
open_gbe_file(void)
{
	struct stat gbe_st;

	xopen(&gbe_fd, fname, gbe_flags, &gbe_st);

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
}

static void
read_gbe_file(void)
{
	size_t p;
	uint8_t do_read[2] = {1, 1};

	/*
	 * The copy, brick and setchecksum commands need
	 * only read data from the user-specified part.
	 *
	 * We can skip reading the other part, thus:
	 */
	if (cmd == CMD_COPY ||
	    cmd == CMD_BRICK ||
	    cmd == CMD_SETCHECKSUM)
		do_read[part ^ 1] = 0;

	for (p = 0; p < 2; p++) {
		if (do_read[p])
			read_gbe_file_part(p);
	}
}

static void
read_gbe_file_part(size_t p)
{
	void *mem_offset = gbe_mem_offset(p ^ command[cmd].invert, "pread");

	read_file_exact(gbe_fd, mem_offset,
	    GBE_PART_SIZE, gbe_file_offset(p, "pread"), fname, "pread");
}

static void
cmd_setmac(void)
{
	size_t partnum;
	uint8_t mac_updated = 0;

	printf("MAC address to be written: %s\n", mac_str);
	parse_mac_string();

	for (partnum = 0; partnum < 2; partnum++)
		mac_updated |= write_mac_part(partnum);

	if (mac_updated)
		errno = 0;
}

static void
parse_mac_string(void)
{
	size_t mac_byte;

	if (strlen(mac_str) != 17)
		err(EINVAL, "MAC address is the wrong length");

	memset(mac_buf, 0, sizeof(mac_buf));

	for (mac_byte = 0; mac_byte < 6; mac_byte++)
		set_mac_byte(mac_byte);

	if ((mac_buf[0] | mac_buf[1] | mac_buf[2]) == 0)
		err(EINVAL, "Must not specify all-zeroes MAC address");

	if (mac_buf[0] & 1)
		err(EINVAL, "Must not specify multicast MAC address");
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

	/* If random, ensure that local/unicast bits are set */
	if ((mac_byte_pos == 0) && (mac_nib_pos == 1) &&
	    ((mac_ch | 0x20) == 'x' ||
	    (mac_ch == '?')))
		hex_num = (hex_num & 0xE) | 2; /* local, unicast */

	/*
	 * Words other than the MAC address are stored little
	 * endian in the file, and we handle that when reading.
	 * However, MAC address words are stored big-endian
	 * in that file, so we write each 2-byte word logically
	 * in little-endian order, which on little-endian would
	 * be stored big-endian in memory, and vice versa.
	 *
	 * Later code using the MAC string will handle this.
	 */

	mac_buf[mac_byte_pos >> 1] |= hex_num <<
	    (((mac_byte_pos & 1) << 3) /* left or right byte? */
	    | ((mac_nib_pos ^ 1) << 2)); /* left or right nib? */
}

static uint16_t
hextonum(char ch_s)
{
	/*
	 * We assume char is signed, hence ch_s.
	 * We explicitly cast to unsigned:
	 */
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
#ifdef HAVE_ARC4RANDOM_BUF
		arc4random_buf(rnum, n);
#else
		read_file_exact(urandom_fd, rnum, n, 0, rname, NULL);
#endif
	}

	return (uint16_t)(rnum[--n] & 0xf);
}

static void
read_file_exact(int fd, void *buf, size_t len,
    off_t off, const char *path, const char *op)
{
	int retry;
	ssize_t rval;

	if (fd == -1)
		err(ECANCELED, "Trying to open bad fd: %s", path);

	for (retry = 0; retry < MAX_RETRY_READ; retry++) {
		if (op)
			rval = pread(fd, buf, len, off);
		else
			rval = read(fd, buf, len);

		if (rval == (ssize_t)len) {
			errno = 0;
			return;
		}

		if (rval != -1)
			err(ECANCELED,
			    "Short %s, %zd bytes, on file: %s",
			    op ? op : "read", rval, path);

		if (errno != EINTR)
			err(ECANCELED,
			    "Could not %s file: '%s'",
			    op ? op : "read", path);
	}

	err(EINTR, "%s: max retries exceeded on file: %s",
	    op ? op : "read", path);
}

static int
write_mac_part(size_t partnum)
{
	size_t w;

	if (!good_checksum(partnum))
		return 0;

	for (w = 0; w < 3; w++)
		set_word(w, partnum, mac_buf[w]);

	printf("Wrote MAC address to part %zu: ", partnum);
	print_mac_address(partnum);

	set_checksum(partnum);

	return 1;
}

static void
cmd_dump(void)
{
	size_t partnum;
	int num_invalid = 0;

	for (partnum = 0; partnum < 2; partnum++) {
		if (!good_checksum(partnum))
			++num_invalid;

		printf("MAC (part %zu): ", partnum);
		print_mac_address(partnum);
		hexdump(partnum);
	}

	if (num_invalid < 2)
		errno = 0;
}

static void
print_mac_address(size_t partnum)
{
	size_t c;

	for (c = 0; c < 3; c++) {
		uint16_t val16 = word(c, partnum);
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
		printf("%08zx ", row << 4);
		for (c = 0; c < 8; c++) {
			val16 = word((row << 3) + c, partnum);
			if (c == 4)
				printf(" ");
			printf(" %02x %02x", val16 & 0xff, val16 >> 8);
		}
		printf("\n");
	}
}

static void
cmd_setchecksum(void)
{
	set_checksum(part);
}

static void
set_checksum(size_t p)
{
	size_t c;
	uint16_t val16 = 0;

	check_part_num(p);

	for (c = 0; c < NVM_CHECKSUM_WORD; c++)
		val16 += word(c, p);

	set_word(NVM_CHECKSUM_WORD, p, NVM_CHECKSUM - val16);
}

static void
cmd_brick(void)
{
	uint16_t checksum_word;

	if (!good_checksum(part)) {
		err(ECANCELED,
		    "Part %zu checksum already invalid in file '%s'",
		    part, fname);
	}

	/*
	 * We know checksum_word is valid, so we need only
	 * flip one bit to invalidate it.
	 */
	checksum_word = word(NVM_CHECKSUM_WORD, part);
	set_word(NVM_CHECKSUM_WORD, part, checksum_word ^ 1);
}

static void
cmd_copy(void)
{
	if (!good_checksum(part ^ 1))
		err(ECANCELED, "copy p%zu, file '%s'", part ^ 1, fname);

	/*
	 * SPEED HACK:
	 *
	 * read_gbe_file() already performed the copy,
	 * by virtue of inverted read. We need
	 * only set the other part as changed.
	 */
	set_part_modified(part ^ 1);
}

static void
cmd_swap(void)
{
	if (!(good_checksum(0) || good_checksum(1)))
		err(ECANCELED, "swap parts, file '%s'", fname);

	/*
	 * good_checksum() can set errno, if one
	 * of the parts is bad. We will reset it.
	 */
	errno = 0;

	/*
	 * SPEED HACK:
	 *
	 * read_gbe_file() already performed the swap,
	 * by virtue of inverted read. We need
	 * only set both parts as changed.
	 */
	set_part_modified(0);
	set_part_modified(1);
}

static int
good_checksum(size_t partnum)
{
	size_t w;
	uint16_t total = 0;

	for (w = 0; w <= NVM_CHECKSUM_WORD; w++)
		total += word(w, partnum);

	if (total == NVM_CHECKSUM)
		return 1;

	fprintf(stderr, "WARNING: BAD checksum in part %zu\n",
	    partnum ^ command[cmd].invert);

	set_err(ECANCELED);
	return 0;
}

/*
 * GbE NVM files store 16-bit (2-byte) little-endian words.
 * We must therefore swap the order when reading or writing.
 */

static uint16_t
word(size_t pos16, size_t p)
{
	size_t pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	return buf[pos] | (buf[pos + 1] << 8);
}

static void
set_word(size_t pos16, size_t p, uint16_t val16)
{
	size_t pos;

	check_nvm_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	buf[pos] = (uint8_t)(val16 & 0xff);
	buf[pos + 1] = (uint8_t)(val16 >> 8);

	set_part_modified(p);
}

static void
check_nvm_bound(size_t c, size_t p)
{
	/*
	 * NVM_SIZE assumed as the limit, because the
	 * current design assumes that we will only
	 * ever modified the NVM area.
	 *
	 * The only exception is copy/swap, but these
	 * do not use word/set_word and therefore do
	 * not cause check_nvm_bound() to be called.
	 *
	 * TODO:
	 * This should be adjusted in the future, if
	 * we ever wish to work on the extented area.
	 */

	check_part_num(p);

	if (c >= NVM_WORDS)
		err(EINVAL, "check_nvm_bound: out of bounds %zu", c);
}

static void
write_gbe_file(void)
{
	size_t p;

	if (gbe_flags == O_RDONLY)
		return;

	for (p = 0; p < 2; p++) {
		if (part_modified[p])
			write_gbe_file_part(p);
	}
}

static void
write_gbe_file_part(size_t p)
{
	ssize_t rval = pwrite(gbe_fd, gbe_mem_offset(p, "pwrite"),
	    GBE_PART_SIZE, gbe_file_offset(p, "pwrite"));

	if (rval == -1)
		err(ECANCELED, "Can't write %zu b to '%s' p%zu",
		    GBE_PART_SIZE, fname, p);

	if (rval != GBE_PART_SIZE)
		err(ECANCELED, "CORRUPTED WRITE (%zd b) to file '%s' p%zu",
		    rval, fname, p);
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

/*
 * This one is similar to gbe_file_offset,
 * but used to check Gbe bounds in memory,
 * and it is *also* used during file I/O.
 */
static void *
gbe_mem_offset(size_t p, const char *f_op)
{
	off_t gbe_off = gbe_x_offset(p, f_op, "mem",
	    GBE_PART_SIZE, GBE_FILE_SIZE);

	return (void *)(buf + gbe_off);
}

static off_t
gbe_x_offset(size_t p, const char *f_op, const char *d_type,
    off_t nsize, off_t ncmp)
{
	off_t off;

	check_part_num(p);

	off = (off_t)p * nsize;

	if (off + GBE_PART_SIZE > ncmp)
		err(ECANCELED, "GbE %s %s out of bounds: %s",
		    d_type, f_op, fname);

	if (off != 0 && off != ncmp >> 1)
		err(ECANCELED, "GbE %s %s at bad offset: %s",
		    d_type, f_op, fname);

	return off;
}

static void
set_part_modified(size_t p)
{
	check_part_num(p);
	part_modified[p] = 1;
}

static void
check_part_num(size_t p)
{
	if (p > 1)
		err(EINVAL, "Bad part number (%zu)", p);
}

static void
usage(void)
{
	const char *util = getnvmprogname();

#ifdef __OpenBSD__
	if (pledge("stdio", NULL) == -1)
		err(ECANCELED, "pledge");
#endif
	fprintf(stderr,
	    "Modify Intel GbE NVM images e.g. set MAC\n"
	    "USAGE:\n"
	    "\t%s FILE dump\n"
	    "\t%s FILE   # same as setmac without [MAC]\n"
	    "\t%s FILE setmac [MAC]\n"
	    "\t%s FILE swap\n"
	    "\t%s FILE copy 0|1\n"
	    "\t%s FILE brick 0|1\n"
	    "\t%s FILE setchecksum 0|1\n",
	    util, util, util, util, util, util, util);

	err(ECANCELED, "Too few arguments");
}

static void
err(int nvm_errval, const char *msg, ...)
{
	va_list args;

	fprintf(stderr, "%s: ", getnvmprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	set_err(nvm_errval);
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
set_err(int x)
{
	if (errno)
		return;
	if (x)
		errno = x;
	else
		errno = ECANCELED;
}
