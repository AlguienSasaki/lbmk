/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org> */
/* Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com> */

#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void set_cmd(int, char **);
void set_io_flags(int, char **);
void openFiles(void);
void checkdir(const char *);
void xopen(int *, const char *, int, struct stat *);
void read_gbe(void);
void read_gbe_part(int, int);
void cmd_setmac(void);
void parse_mac_string(void);
void set_mac_byte(int, uint64_t *);
void check_mac_separator(int);
void set_mac_nib(int, int, uint8_t *);
uint8_t hextonum(char);
uint8_t rhex(void);
int write_mac_part(int);
void cmd_dump(void);
void print_mac_address(int);
void hexdump(int);
void cmd_setchecksum(void);
void cmd_brick(void);
void cmd_copy(void);
void cmd_swap(void);
int good_checksum(int);
uint16_t word(int, int);
void set_word(int, int, uint16_t);
void check_bound(int, int);
void write_gbe(void);
void write_gbe_part(int);
void swap(int);
void usage(char *);
void err_if(int);
int set_err(int);

#define COMMAND argv[2]
#define MAC_ADDRESS argv[3]
#define PARTN argv[3]
#define NVM_CHECKSUM 0xBABA
#define NVM_CHECKSUM_WORD 0x3F
#define NVM_SIZE 128

#define SIZE_4KB 0x1000
#define SIZE_8KB 0x2000
#define SIZE_16KB 0x4000
#define SIZE_128KB 0x20000

#define items(x) (sizeof((x)) / sizeof((x)[0]))

uint8_t buf[SIZE_8KB];
uint16_t macbuf[3] = {0, 0, 0};
size_t partsize;

int flags;
int rfd;
int fd;
int part;
int invert = 0;
int part_modified[2] = {0, 0};

const char *mac = NULL;
const char *rmac = "xx:xx:xx:xx:xx:xx";
const char *fname = "";

typedef struct op {
	const char *str;
	void (*cmd)(void);
	int args;
} op_t;
op_t op[] = {
	{ "dump", cmd_dump, 3 },
	{ "setmac", cmd_setmac, 3 },
	{ "swap", cmd_swap, 3 },
	{ "copy", cmd_copy, 4 },
	{ "brick", cmd_brick, 4 },
	{ "setchecksum", cmd_setchecksum, 4 },
};

void (*cmd)(void) = NULL;

int
main(int argc, char *argv[])
{
	if (argc >= 2)
		fname = argv[1];
#ifdef __OpenBSD__
	err_if(pledge("stdio rpath wpath unveil", NULL) == -1);
	err_if(unveil("/dev/urandom", "r") == -1);
#endif
	set_cmd(argc, argv);
	set_io_flags(argc, argv);
#ifdef __OpenBSD__
	if (flags == O_RDONLY) {
		err_if(unveil(fname, "r") == -1);
		err_if(unveil(NULL, NULL) == -1);
		err_if(pledge("stdio rpath", NULL) == -1);
	} else {
		err_if(unveil(fname, "rw") == -1);
		err_if(unveil(NULL, NULL) == -1);
		err_if(pledge("stdio rpath wpath", NULL) == -1);
	}
#endif
	openFiles();
#ifdef __OpenBSD__
	err_if(pledge("stdio", NULL) == -1);
#endif
	read_gbe();
	(*cmd)();
	write_gbe();

	err_if((errno != 0) && (cmd != cmd_dump));
	return errno ? EXIT_FAILURE : EXIT_SUCCESS;
}

void
set_cmd(int argc, char *argv[])
{
	size_t i;

	if (argc < 2) {
		usage(argv[0]);
	} else if (argc > 2) {
		for (i = 0; (i < items(op)) && (cmd == NULL); i++) {
			if (strcmp(COMMAND, op[i].str) != 0)
				continue;
			if (argc >= op[i].args) {
				cmd = op[i].cmd;
				break;
			}
			err(set_err(EINVAL), "Too few args on command '%s'",
			    op[i].str);
		}
	} else { /* argc == 2 */
		cmd = cmd_setmac;
	}

	if ((cmd == NULL) && (argc > 2)) { /* nvm gbe [MAC] */
		mac = COMMAND;
		cmd = cmd_setmac;
	} else if (cmd == cmd_setmac) { /* nvm gbe setmac [MAC] */
		mac = rmac; /* random MAC */
		if (argc > 3)
			mac = MAC_ADDRESS;
	} else if ((cmd != NULL) && (argc > 3)) { /* user-supplied partnum */
		err_if((errno = (!((part = PARTN[0] - '0') == 0 || part == 1))
		    || PARTN[1] ? EINVAL : errno)); /* only allow '0' or '1' */
	}
	err_if((errno = (cmd == NULL) ? EINVAL : errno));
}

void
set_io_flags(int argc, char *argv[])
{
	flags = O_RDWR;
	if (argc > 2)
		if (strcmp(COMMAND, "dump") == 0)
			flags = O_RDONLY;
}

void
openFiles(void)
{
	struct stat st;
	struct stat st_rfd;

	checkdir("/dev/urandom");
	checkdir(fname);

	xopen(&rfd, "/dev/urandom", O_RDONLY, &st_rfd);
	xopen(&fd, fname, flags, &st);

	switch(st.st_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		partsize = st.st_size >> 1;
		break;
	default:
		err(set_err(ECANCELED), "Invalid file size (not 8/16/128KiB)");
		break;
	}
}

void
checkdir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == -1)
		err(set_err(ECANCELED), "%s", path);
	if (S_ISDIR(st.st_mode))
		err(set_err(EISDIR), "%s", path);
}

void
xopen(int *f, const char *l, int p, struct stat *st)
{
	if ((*f = open(l, p)) == -1)
		err(set_err(ECANCELED), "%s", l);
	if (fstat(*f, st) == -1)
		err(set_err(ECANCELED), "%s", l);
}

void
read_gbe(void)
{
	int p;
	int do_read[2] = {1, 1};

	if ((cmd == cmd_copy) || (cmd == cmd_brick) ||
	    (cmd == cmd_setchecksum))
		do_read[part ^ 1] = 0;

	/*
	 * speedhack: if copy/swap, flip where data gets written to memory,
	 * so that cmd_copy and cmd_swap don't have to work on every word
	 */
	if ((cmd == cmd_copy) || (cmd == cmd_swap))
		invert = 1;

	for (p = 0; p < 2; p++)
		if (do_read[p])
			read_gbe_part(p, invert);
}

void
read_gbe_part(int p, int invert)
{
	if (pread(fd, buf + (SIZE_4KB * (p ^ invert)), SIZE_4KB, p * partsize)
	    != SIZE_4KB)
		err(set_err(ECANCELED),
		    "Can't read %d b from '%s' p%d", SIZE_4KB, fname, p);
	swap(p ^ invert); /* handle big-endian host CPU */
}

void
cmd_setmac(void)
{
	int partnum;
	int mac_updated = 0;
	parse_mac_string();

	printf("MAC address to be written: %s\n", mac);

	for (partnum = 0; partnum < 2; partnum++)
		mac_updated |= write_mac_part(partnum);
	if (mac_updated)
		errno = 0;
}

void
parse_mac_string(void)
{
	int mac_pos;
	uint64_t total = 0;

	if (strnlen(mac, 20) != 17)
		err(set_err(EINVAL), "Invalid MAC address string length");

	for (mac_pos = 0; mac_pos < 16; mac_pos += 3)
		set_mac_byte(mac_pos, &total);

	if (total == 0)
		err(set_err(EINVAL), "Invalid MAC (all-zero MAC address)");
	if (macbuf[0] & 1)
		err(set_err(EINVAL), "Invalid MAC (multicast bit set)");
}

void
set_mac_byte(int mac_pos, uint64_t *total)
{
	int nib;
	uint8_t h = 0;
	check_mac_separator(mac_pos);

	for (nib = 0; nib < 2; nib++, *total += h)
		set_mac_nib(mac_pos, nib, &h);
}

void
check_mac_separator(int mac_pos)
{
	if (mac_pos == 15)
		return;
	char separator = mac[mac_pos + 2];
	if (separator == ':')
		return;
	err(set_err(EINVAL), "Invalid MAC address separator '%c'", separator);
}

void
set_mac_nib(int mac_pos, int nib, uint8_t *h)
{
	int byte = mac_pos / 3;

	if ((*h = hextonum(mac[mac_pos + nib])) > 15)
		err(set_err(EINVAL), "Invalid character '%c'",
		    mac[mac_pos + nib]);

	/* If random, ensure that local/unicast bits are set */
	if ((byte == 0) && (nib == 1))
		if ((mac[mac_pos + nib] == '?') ||
		    (mac[mac_pos + nib] == 'x') ||
		    (mac[mac_pos + nib] == 'X')) /* random */
			*h = (*h & 0xE) | 2; /* local, unicast */

	macbuf[byte >> 1] |= ((uint16_t ) *h) << ((8 * (byte % 2)) +
	    (4 * (nib ^ 1)));
}

uint8_t
hextonum(char ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	else if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	else if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	else if ((ch == '?') || (ch == 'x') || (ch == 'X'))
		return rhex(); /* random hex value */
	return 16; /* error: invalid character */
}

uint8_t
rhex(void)
{
	static uint8_t n = 0, rnum[16];
	if (!n)
		err_if(read(rfd, (uint8_t *) &rnum, (n = 15) + 1) == -1);
	return rnum[n--] & 0xf;
}

int
write_mac_part(int partnum)
{
	int w;

	part = partnum;
	if (!good_checksum(partnum))
		return 0;

	for (w = 0; w < 3; w++)
		set_word(w, partnum, macbuf[w]);

	printf("Wrote MAC address to part %d: ", partnum);
	print_mac_address(partnum);

	cmd_setchecksum();
	return 1;
}

void
cmd_dump(void)
{
	int partnum;
	int num_invalid = 0;

	for (partnum = 0; partnum < 2; partnum++) {
		if (!good_checksum(partnum))
			++num_invalid;

		printf("MAC (part %d): ", partnum);
		print_mac_address(partnum);
		hexdump(partnum);
	}

	if ((num_invalid < 2))
		errno = 0;
}

void
print_mac_address(int partnum)
{
	int c;
	for (c = 0; c < 3; c++) {
		uint16_t val16 = word(c, partnum);
		printf("%02x:%02x", val16 & 0xff, val16 >> 8);
		if (c == 2)
			printf("\n");
		else
			printf(":");
	}
}

void
hexdump(int partnum)
{
	int c;
	int row;

	for (row = 0; row < 8; row++) {
		printf("%08x ", row << 4);
		for (c = 0; c < 8; c++) {
			uint16_t val16 = word((row << 3) + c, partnum);
			if (c == 4)
				printf(" ");
			printf(" %02x %02x", val16 & 0xff, val16 >> 8);
		}
		printf("\n");
	}
}

void
cmd_setchecksum(void)
{
	int c;
	uint16_t val16 = 0;

	for (c = 0; c < NVM_CHECKSUM_WORD; c++)
		val16 += word(c, part);

	set_word(NVM_CHECKSUM_WORD, part, NVM_CHECKSUM - val16);
}

void
cmd_brick(void)
{
	if (good_checksum(part))
		set_word(NVM_CHECKSUM_WORD, part,
		    ((word(NVM_CHECKSUM_WORD, part)) ^ 0xFF));
}

void
cmd_copy(void)
{
	err_if(!good_checksum(part ^ 1));
	part_modified[part ^ 1] = 1;
}

void
cmd_swap(void) {
	err_if(!(good_checksum(0) || good_checksum(1)));
	errno = 0;

	part_modified[1] = part_modified[0] = 1;
}

int
good_checksum(int partnum)
{
	int w;
	uint16_t total = 0;
	for (w = 0; w <= NVM_CHECKSUM_WORD; w++)
		total += word(w, partnum);

	if (total == NVM_CHECKSUM)
		return 1;

	fprintf(stderr, "WARNING: BAD checksum in part %d\n",
	    partnum ^ invert);
	(void) set_err(ECANCELED);
	return 0;
}

uint16_t
word(int pos16, int p)
{
	check_bound(pos16, p);
	return ((uint16_t *) (buf + (SIZE_4KB * p)))[pos16];
}

void
set_word(int pos16, int p, uint16_t val16)
{
	check_bound(pos16, p);
	part_modified[p] = 1;
	((uint16_t *) (buf + (SIZE_4KB * p)))[pos16] = val16;
}

void
check_bound(int c, int p)
{
	if ((p != 0) && (p != 1))
		err(set_err(EINVAL), "check_bound: invalid partnum %d", p);
	if ((c < 0) || (c >= (SIZE_4KB >> 1)))
		err(set_err(EINVAL), "check_bound: out of bounds %d", c);
}

void
write_gbe(void)
{
	int p;

	if (flags != O_RDONLY)
		for (p = 0; p < 2; p++)
			if (part_modified[p])
				write_gbe_part(p);
	err_if(close(fd) == -1);
}

void
write_gbe_part(int p)
{
	swap(p); /* swap bytes on big-endian host CPUs */
	if(pwrite(fd, buf + (SIZE_4KB * p), SIZE_4KB, p * partsize)
	    != SIZE_4KB)
		err(set_err(ECANCELED),
		    "Can't write %d b to '%s' p%d", SIZE_4KB, fname, p);
}

void
swap(int partnum)
{
	size_t w;
	size_t x;

	int e = 1;
	uint8_t *n = buf + (SIZE_4KB * partnum);

	for (w = NVM_SIZE * ((uint8_t *) &e)[0], x = 1;
	    w < NVM_SIZE; w += 2, x += 2) {
		uint8_t chg = n[w];
		n[w] = n[x];
		n[x] = chg;
	}
}

void
usage(char *util)
{
#ifdef __OpenBSD__
	err_if(pledge("stdio", NULL) == -1);
#endif
	fprintf(stderr,
	    "Modify Intel GbE NVM images e.g. set MAC\n"
	    "USAGE:\n"
	    "%s FILE dump\n"
	    " %s FILE\n # same as setmac without arg\n"
	    " %s FILE setmac [MAC]\n"
	    " %s FILE swap\n"
	    " %s FILE copy 0|1\n"
	    " %s FILE brick 0|1\n"
	    " %s FILE setchecksum 0|1\n",
	    util, util, util, util, util, util, util);
	err(set_err(ECANCELED), "Too few arguments");
}

void
err_if(int x)
{
	if (x)
		err(set_err(ECANCELED), "%s", fname);
}

int
set_err(int x)
{
	errno = errno ? errno : x;
	return EXIT_FAILURE;
}
