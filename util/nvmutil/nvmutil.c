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
#ifndef HAVE_ARC4RANDOM
#define HAVE_ARC4RANDOM
#endif
#endif

static void reset_global_state(void);
static void set_cmd(int, char **);
static void check_cmd_args(int, char **);
static void set_io_flags(int, char **);
static void open_gbe_file(void);
#ifndef HAVE_ARC4RANDOM
static void open_dev_urandom(void);
#endif
static void xopen(int *, const char *, int, struct stat *);
static void read_gbe(void);
static void read_gbe_part(size_t, unsigned char);
static void cmd_setmac(void);
static void parse_mac_string(void);
static void set_mac_byte(size_t);
static void check_mac_separator(size_t);
static void set_mac_nib(size_t, size_t);
static uint8_t hextonum(char);
static uint8_t rhex(void);
static void xread(int, void *, size_t,
    off_t, const char *, const char *);
static int write_mac_part(size_t);
static void cmd_dump(void);
static void print_mac_address(size_t);
static void hexdump(size_t);
static void cmd_setchecksum(void);
static void set_checksum(size_t);
static void cmd_brick(void);
static void cmd_copy(void);
static void cmd_swap(void);
static int good_checksum(size_t);
static uint16_t word(size_t, size_t);
static void set_word(size_t, size_t, uint16_t);
static void check_bound(size_t, size_t);
static void write_gbe(void);
static void write_gbe_part(size_t);
static off_t gbe_file_offset(size_t, const char *);
static void *gbe_mem_offset(size_t, const char *);
static off_t gbe_x_offset(size_t, const char *, const char *, off_t, off_t);
static void usage(void);
static void err(int, const char *, ...);
static const char *getnvmprogname(void);
static void set_err(int);

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
 * Portably macro based on BSD nitems.
 * Used to count the number of commands (see below).
 */
#define items(x) (sizeof((x)) / sizeof((x)[0]))

static const char newrandom[] = "/dev/urandom";
static const char oldrandom[] = "/dev/random"; /* fallback on OLD unix */
#ifndef HAVE_ARC4RANDOM
static const char *rname = NULL;
#endif

static uint8_t buf[GBE_FILE_SIZE]; /* 8KB */
static uint16_t mac_buf[3];
static off_t partsize;

static int flags;
#ifndef HAVE_ARC4RANDOM
static int rfd = -1;
#endif
static int fd = -1;
static struct stat st;
static size_t part;
static unsigned char invert;
static unsigned char part_modified[2];

static const char *mac_str = NULL;
static const char rmac[] = "xx:xx:xx:xx:xx:xx";
static const char *fname = "";
static const char *argv0;

struct commands {
	const char *str;
	void (*cmd)(void);
	int args;
};
static const struct commands command[] = {
	{ "dump", cmd_dump, 3 },
	{ "setmac", cmd_setmac, 3 },
	{ "swap", cmd_swap, 3 },
	{ "copy", cmd_copy, 4 },
	{ "brick", cmd_brick, 4 },
	{ "setchecksum", cmd_setchecksum, 4 },
};

static void (*cmd)(void) = NULL;

int
main(int argc, char *argv[])
{
	argv0 = argv[0];
	if (argc < 2)
		usage();

	reset_global_state();
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
	if (flags == O_RDONLY) {
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

#ifndef HAVE_ARC4RANDOM
	open_dev_urandom();
#endif
	open_gbe_file();

#ifdef __OpenBSD__
	if (pledge("stdio", NULL) == -1)
		err(ECANCELED, "pledge stdio (main)");
#endif

	read_gbe();
	(*cmd)();
	write_gbe();

	if (close(fd) == -1)
		err(ECANCELED, "close '%s'", fname);
#ifndef HAVE_ARC4RANDOM
	if (close(rfd) == -1)
		err(ECANCELED, "close '%s'", rname);
#endif

	if (cmd != cmd_dump) {
		if (errno)
			err(ECANCELED, "Unhandled error on exit");
	}

	if (errno)
		return EXIT_FAILURE;
	else
		return EXIT_SUCCESS;
}

/*
 * Currently redundant, because the program only runs
 * once, but I plan to expand this tool so that it can
 * work on multiple files, using getop switches as args.
 */
static void
reset_global_state(void)
{
	errno = 0;

	mac_str = NULL;
	invert = 0;
	part_modified[0] = 0;
	part_modified[1] = 0;
	fname = "";
	cmd = NULL;
	fd = -1;
#ifndef HAVE_ARC4RANDOM
	rfd = -1;
#endif
	part = 0;

	memset(mac_buf, 0, sizeof(mac_buf));
	memset(buf, 0, sizeof(buf));
}

static void
set_cmd(int argc, char *argv[])
{
	size_t i;

	if (argc == 2) {
		cmd = cmd_setmac;
		return;
	}

	for (i = 0; i < items(command) && cmd == NULL; i++) {
		if (strcmp(argv[2], command[i].str) != 0)
			continue;
		if (argc >= command[i].args) {
			cmd = command[i].cmd;
			break;
		}
		err(EINVAL, "Too few args: command '%s'", command[i].str);
	}
}

static void
check_cmd_args(int argc, char *argv[])
{
	if (cmd == NULL && argc > 2) { /* nvm gbe [MAC] */
		mac_str = argv[2];
		cmd = cmd_setmac;
	} else if (cmd == cmd_setmac) { /* nvm gbe setmac [MAC] */
		mac_str = rmac; /* random MAC */
		if (argc > 3)
			mac_str = argv[3];
	} else if (cmd != NULL && argc > 3) { /* user-supplied partnum */
		part = argv[3][0] - '0';
		if (!((part == 0 || part == 1) && argv[3][1] == '\0'))
			err(EINVAL, "Bad partnum: %s", argv[3]);
	}

	if (cmd == NULL)
		err(EINVAL, "Bad command");
}

static void
set_io_flags(int argc, char *argv[])
{
	flags = O_RDWR;

	if (argc < 3)
		return;

	if (strcmp(argv[2], "dump") == 0)
		flags = O_RDONLY;
}

#ifndef HAVE_ARC4RANDOM
static void
open_dev_urandom(void)
{
	struct stat st_rfd;

	rname = newrandom;

	if ((rfd = open(rname, O_RDONLY)) == -1) {
		/*
		 * Fall back to /dev/random on old platforms
		 * where /dev/urandom does not exist.
		 */
		rname = oldrandom;
		xopen(&rfd, rname, O_RDONLY, &st_rfd);
	}
}
#endif

static void
open_gbe_file(void)
{
	xopen(&fd, fname, flags, &st);

	switch(st.st_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		partsize = st.st_size >> 1;
		break;
	default:
		err(ECANCELED, "File size must be 8KB, 16KB or 128KB");
		break;
	}
}

static void
xopen(int *f, const char *l, int p, struct stat *st)
{
	if ((*f = open(l, p)) == -1)
		err(ECANCELED, "%s", l);
	if (fstat(*f, st) == -1)
		err(ECANCELED, "%s", l);
}

static void
read_gbe(void)
{
	size_t p;
	int do_read[2] = {1, 1};

	if (cmd == cmd_copy || cmd == cmd_brick || cmd == cmd_setchecksum)
		do_read[part ^ 1] = 0;

	/*
	 * speedhack: if copy/swap, flip where data gets written to memory,
	 * so that cmd_copy and cmd_swap don't have to work on every word
	 */
	if (cmd == cmd_copy || cmd == cmd_swap)
		invert = 1;

	for (p = 0; p < 2; p++) {
		if (do_read[p])
			read_gbe_part(p, invert);
	}
}

static void
read_gbe_part(size_t p, unsigned char invert)
{
	xread(fd, gbe_mem_offset(p ^ invert, "pread"),
	    GBE_PART_SIZE, gbe_file_offset(p, "pread"), fname, "pread");
}

static void
cmd_setmac(void)
{
	size_t partnum;
	unsigned char mac_updated = 0;

	parse_mac_string();
	printf("MAC address to be written: %s\n", mac_str);

	for (partnum = 0; partnum < 2; partnum++)
		mac_updated |= write_mac_part(partnum);

	if (mac_updated)
		errno = 0;
}

static void
parse_mac_string(void)
{
	size_t mac_str_pos;

	if (strlen(mac_str) != 17)
		err(EINVAL, "MAC address is the wrong length");

	for (mac_str_pos = 0; mac_str_pos < 16; mac_str_pos += 3)
		set_mac_byte(mac_str_pos);

	if ((mac_buf[0] | mac_buf[1] | mac_buf[2]) == 0)
		err(EINVAL, "Must not specify all-zeroes MAC address");

	if (mac_buf[0] & 1)
		err(EINVAL, "Must not specify multicast MAC address");
}

static void
set_mac_byte(size_t mac_str_pos)
{
	size_t mac_nib_pos;

	check_mac_separator(mac_str_pos);

	for (mac_nib_pos = 0; mac_nib_pos < 2; mac_nib_pos++)
		set_mac_nib(mac_str_pos, mac_nib_pos);
}

static void
check_mac_separator(size_t mac_str_pos)
{
	char separator;

	if (mac_str_pos == 15)
		return;
	if ((separator = mac_str[mac_str_pos + 2]) == ':')
		return;

	err(EINVAL, "Invalid MAC address separator '%c'", separator);
}

static void
set_mac_nib(size_t mac_str_pos, size_t mac_nib_pos)
{
	uint8_t mac_ch;

	size_t mac_byte_pos;

	size_t mac_buf_word_pos;
	size_t mac_buf_byte_pos;
	size_t mac_buf_nib_pos;

	if ((mac_ch = hextonum(mac_str[mac_str_pos + mac_nib_pos])) > 15)
		err(EINVAL, "Invalid character '%c'",
		    mac_str[mac_str_pos + mac_nib_pos]);

	mac_byte_pos = mac_str_pos / 3;

	/* If random, ensure that local/unicast bits are set */
	if ((mac_byte_pos == 0) && (mac_nib_pos == 1) &&
	    ((mac_str[mac_str_pos + mac_nib_pos] == '?') ||
	    (mac_str[mac_str_pos + mac_nib_pos] == 'x') ||
	    (mac_str[mac_str_pos + mac_nib_pos] == 'X'))) /* random */
		mac_ch = (mac_ch & 0xE) | 2; /* local, unicast */

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
	mac_buf_word_pos = mac_byte_pos >> 1;
	mac_buf_byte_pos = (mac_byte_pos & 1) << 3; /* left or right byte? */
	mac_buf_nib_pos = (mac_nib_pos ^ 1) << 2; /* left or right nib? */

	/*
	 * Now we can shift properly, OR'ing the result:
	 */
	mac_buf[mac_buf_word_pos] |= (uint16_t)mac_ch <<
	    (mac_buf_byte_pos | mac_buf_nib_pos);
}

static uint8_t
hextonum(char mac_ch)
{
	if ((unsigned)(mac_ch - '0') <= 9)
		return mac_ch - '0';

	mac_ch |= 0x20;

	if ((unsigned)(mac_ch - 'a') <= 5)
		return mac_ch - 'a' + 10;
	else if (mac_ch == '?' || mac_ch == 'x')
		return rhex(); /* random character */
	else
		return 16; /* invalid character */
}

static uint8_t
rhex(void)
{
	static size_t n = 0;
	static uint8_t rnum[12];

	if (!n) {
		n = sizeof(rnum);
#ifdef HAVE_ARC4RANDOM
		arc4random_buf(rnum, n);
#else
		xread(rfd, rnum, n, 0, rname, NULL);
#endif
	}

	return rnum[--n] & 0xf;
}

static void
xread(int fd, void *buf, size_t len,
    off_t off, const char *path, const char *op)
{
	int retry;
	ssize_t rval;

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

	if (p > 1)
		err(ECANCELED, "Bad part num %zu (must be 0 or 1)", p);

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
	 * read_gbe() already performed the copy,
	 * by virtue of inverted read. We need
	 * only set the other part as changed.
	 */
	part_modified[part ^ 1] = 1;
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
	 * read_gbe() already performed the swap,
	 * by virtue of inverted read. We need
	 * only set both parts as changed.
	 */
	part_modified[1] = part_modified[0] = 1;
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
	    partnum ^ invert);

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

	check_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	return buf[pos] | (buf[pos + 1] << 8);
}

static void
set_word(size_t pos16, size_t p, uint16_t val16)
{
	size_t pos;

	check_bound(pos16, p);
	pos = (pos16 << 1) + (p * GBE_PART_SIZE);

	buf[pos] = (uint8_t)(val16 & 0xff);
	buf[pos + 1] = (uint8_t)(val16 >> 8);

	part_modified[p] = 1;
}

static void
check_bound(size_t c, size_t p)
{
	/*
	 * NVM_SIZE assumed as the limit, because the
	 * current design assumes that we will only
	 * ever modified the NVM area.
	 *
	 * The only exception is copy/swap, but these
	 * do not use word/set_word and therefore do
	 * not cause check_bound() to be called.
	 *
	 * TODO:
	 * This should be adjusted in the future, if
	 * we ever wish to work on the extented area.
	 */

	if (p != 0 && p != 1)
		err(EINVAL, "check_bound: invalid partnum %zu", p);
	if (c >= NVM_WORDS)
		err(EINVAL, "check_bound: out of bounds %zu", c);
}

static void
write_gbe(void)
{
	size_t p;

	if (flags == O_RDONLY)
		return;

	for (p = 0; p < 2; p++) {
		if (part_modified[p])
			write_gbe_part(p);
	}
}

static void
write_gbe_part(size_t p)
{
	ssize_t rval = pwrite(fd, gbe_mem_offset(p, "pwrite"),
	    GBE_PART_SIZE, gbe_file_offset(p, "pwrite"));

	if (rval == -1)
		err(ECANCELED, "Can't write %zu b to '%s' p%zu",
		    GBE_PART_SIZE, fname, p);

	if (rval != GBE_PART_SIZE)
		err(ECANCELED, "CORRUPTED WRITE (%zd b) to file '%s' p%zu",
		    rval, fname, p);
}

/*
 * Reads to GbE from write_gbe_part and read_gbe_part
 * are filtered through here. These operations must
 * only write from the 0th position or the half position
 * within the GbE file, and write 4KB of data.
 *
 * This check is called, to ensure just that.
 */
static off_t
gbe_file_offset(size_t p, const char *f_op)
{
	return gbe_x_offset(p, f_op, "file",
	    partsize, st.st_size);
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

	if (p > 1)
		err(ECANCELED, "GbE %s %s invalid partnum: %s",
		    d_type, f_op, fname);

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
