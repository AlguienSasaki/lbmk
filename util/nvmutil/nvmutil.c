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

void cmd_setchecksum(void), cmd_brick(void), swap(int partnum), writeGbe(void),
    cmd_dump(void), cmd_setmac(void), readGbe(void),
    set_mac_nib(int, int, uint8_t *),
    checkdir(const char *path), macf(int partnum), hexdump(int partnum),
    parseMacString(const char *strMac, uint16_t *mac), cmd_swap(void),
    openFiles(void), cmd_copy(void), writeGbe_part(int),
    readGbe_part(int), usage(char*), set_io_flags(int, char **),
    set_cmd(int, char **), setWord(int, int, uint16_t), check_bounds(int, int),
    xopen (int *, const char *, int p, struct stat *),
    set_mac_byte(int, uint64_t *), check_mac_separator(int);
int goodChecksum(int partnum);
uint8_t hextonum(char chs), rhex(void);
uint16_t word(int, int);

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

uint8_t buf[SIZE_8KB];
uint16_t mac[3] = {0, 0, 0};
size_t partsize;
int flags, rfd, fd, part, e = 1;

const char *strMac = NULL, *strRMac = "xx:xx:xx:xx:xx:xx", *fname = NULL;

typedef struct op {
	char *str;
	void (*cmd)(void);
	int args;
} op_t;
op_t op[] = {
{ .str = "dump", .cmd = cmd_dump, .args = 3 },
{ .str = "setmac", .cmd = cmd_setmac, .args = 3 },
{ .str = "swap", .cmd = cmd_swap, .args = 3 },
{ .str = "copy", .cmd = cmd_copy, .args = 4 },
{ .str = "brick", .cmd = cmd_brick, .args = 4 },
{ .str = "setchecksum", .cmd = cmd_setchecksum, .args = 4 },
};
void (*cmd)(void) = NULL;

#define set_err(x) errno = errno ? errno : x
#define err_if(x) if (x) err(set_err(ECANCELED), "%s", fname)

int
main(int argc, char *argv[])
{
#ifdef __OpenBSD__
	err_if(pledge("stdio rpath wpath unveil", NULL) == -1);
	err_if(unveil("/dev/urandom", "r") == -1);
#endif
	set_cmd(argc, argv);

	fname = argv[1];
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
	readGbe();
	(*cmd)();
	writeGbe();

	err_if((errno != 0) && (cmd != cmd_dump));
	return errno;
}

void
set_cmd(int argc, char *argv[])
{
	if (argc < 2) {
		usage(argv[0]);
	} else if (argc > 2) {
		for (int i = 0; (i < 6) && (cmd == NULL); i++) {
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
		strMac = COMMAND;
		cmd = cmd_setmac;
	} else if (cmd == cmd_setmac) { /* nvm gbe setmac [MAC] */
		strMac = strRMac; /* random MAC */
		if (argc > 3)
			strMac = MAC_ADDRESS;
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
checkdir(const char *path)
{
	if (opendir(path) != NULL)
		err(set_err(EISDIR), "%s", path);
	if (errno == ENOTDIR)
		errno = 0;
	err_if(errno);
}

void
openFiles(void)
{
	struct stat st;

	checkdir("/dev/urandom");
	checkdir(fname);

	xopen(&rfd, "/dev/urandom", O_RDONLY, &st);
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
xopen(int *f, const char *l, int p, struct stat *st)
{
	if ((*f = open(l, p)) == -1) \
	    err(set_err(ECANCELED), "%s", l); \
	if (fstat(*f, st) == -1) \
	    err(set_err(ECANCELED), "%s", l);
}

void
readGbe(void)
{
	for (int p = 0; p < 2; p++)
		readGbe_part(p);
}

void
readGbe_part(int p)
{
	if (pread(fd, buf + (SIZE_4KB * p), SIZE_4KB, p * partsize) != SIZE_4KB)
		err(set_err(ECANCELED),
		    "Can't read %d b from '%s' p%d", SIZE_4KB, fname, p);
	swap(p); /* handle big-endian host CPU */
}

void
cmd_setmac(void)
{
	int mac_updated = 0;
	parseMacString(strMac, mac);

	printf("MAC address to be written: %s\n", strMac);

	for (int partnum = 0; partnum < 2; partnum++) {
		if (!goodChecksum(part = partnum))
			continue;

		for (int w = 0; w < 3; w++)
			setWord(w, partnum, mac[w]);

		printf("Wrote MAC address to part %d: ", partnum);
		macf(partnum);

		cmd_setchecksum();
		mac_updated = 1;
	}

	if (mac_updated)
		errno = 0;
}

void
parseMacString(const char *strMac, uint16_t *mac)
{
	uint64_t total = 0;
	if (strnlen(strMac, 20) != 17)
		err(set_err(EINVAL), "Invalid MAC address string length");

	for (int strMacPos = 0; strMacPos < 16; strMacPos += 3)
		set_mac_byte(strMacPos, &total);

	if (total == 0)
		err(set_err(EINVAL), "Invalid MAC (all-zero MAC address)");
	if (mac[0] & 1)
		err(set_err(EINVAL), "Invalid MAC (multicast bit set)");
}

void
set_mac_byte(int strMacPos, uint64_t *total)
{
	uint8_t h = 0;
	check_mac_separator(strMacPos);

	for (int nib = 0; nib < 2; nib++, *total += h)
		set_mac_nib(strMacPos, nib, &h);
}

void
check_mac_separator(int strMacPos)
{
	if (strMacPos == 15)
		return;
	char separator = strMac[strMacPos + 2];
	if (separator == ':')
		return;
	err(set_err(EINVAL), "Invalid MAC address separator '%c'", separator);
}

void
set_mac_nib(int strMacPos, int nib, uint8_t *h)
{
	int byte = strMacPos / 3;

	if ((*h = hextonum(strMac[strMacPos + nib])) > 15)
		err(set_err(EINVAL), "Invalid character '%c'",
		    strMac[strMacPos + nib]);

	/* If random, ensure that local/unicast bits are set */
	if ((byte == 0) && (nib == 1))
		if ((strMac[strMacPos + nib] == '?') ||
		    (strMac[strMacPos + nib] == 'x') ||
		    (strMac[strMacPos + nib] == 'X')) /* random */
			*h = (*h & 0xE) | 2; /* local, unicast */

	mac[byte >> 1] |= ((uint16_t ) *h) << ((8 * (byte % 2)) +
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
		err_if(pread(rfd, (uint8_t *) &rnum, (n = 15) + 1, 0) == -1);
	return rnum[n--] & 0xf;
}

void
cmd_dump(void)
{
	for (int partnum = 0, numInvalid = 0; partnum < 2; partnum++) {
		if (!goodChecksum(partnum))
			++numInvalid;

		printf("MAC (part %d): ", partnum);
		macf(partnum);
		hexdump(partnum);

		if ((numInvalid < 2) && (partnum))
			errno = 0;
	}
}

void
macf(int partnum)
{
	for (int c = 0; c < 3; c++) {
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
	for (int row = 0; row < 8; row++) {
		printf("%08x ", row << 4);
		for (int c = 0; c < 8; c++) {
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
	uint16_t val16 = 0;
	for (int c = 0; c < NVM_CHECKSUM_WORD; c++)
		val16 += word(c, part);

	setWord(NVM_CHECKSUM_WORD, part, NVM_CHECKSUM - val16);
}

void
cmd_brick(void)
{
	if (goodChecksum(part))
		setWord(NVM_CHECKSUM_WORD, part,
		    ((word(NVM_CHECKSUM_WORD, part)) ^ 0xFF));
}

void
cmd_copy(void)
{
	err_if(!goodChecksum(part));
	for (int c = 0; c < (SIZE_4KB >> 1); c++)
		setWord(c, part ^ 1, word(c, part));
}

void
cmd_swap(void) {
	err_if(!(goodChecksum(0) || goodChecksum(1)));
	errno = 0;

	for (int c = 0; c < (SIZE_4KB >> 1); c++) {
		uint16_t chg = word(c, 0);
		setWord(c, 0, word(c, 1));
		setWord(c, 1, chg);
	}
}

int
goodChecksum(int partnum)
{
	uint16_t total = 0;
	for(int w = 0; w <= NVM_CHECKSUM_WORD; w++)
		total += word(w, partnum);

	if (total == NVM_CHECKSUM)
		return 1;

	fprintf(stderr, "WARNING: BAD checksum in part %d\n", partnum);
	set_err(ECANCELED);
	return 0;
}

uint16_t
word(int pos16, int p)
{
	check_bounds(pos16, p);
	return ((uint16_t *) (buf + (SIZE_4KB * p)))[pos16];
}

void
setWord(int pos16, int p, uint16_t val16)
{
	check_bounds(pos16, p);
	((uint16_t *) (buf + (SIZE_4KB * p)))[pos16] = val16;
}

void
check_bounds(int c, int p)
{
	if ((p != 0) && (p != 1))
		err(set_err(EINVAL), "check_bounds: invalid partnum %d", p);
	if ((c < 0) || (c >= (SIZE_4KB >> 1)))
		err(set_err(EINVAL), "check_bounds: out of bounds %d", c);
}

void
writeGbe(void)
{
	if (flags != O_RDONLY)
		for (int p = 0; p < 2; p++)
			writeGbe_part(p);
	err_if(close(fd) == -1);
}

void
writeGbe_part(int p)
{
	swap(p); /* swap bytes on big-endian host CPUs */
	if(pwrite(fd, buf + (SIZE_4KB * p), SIZE_4KB, p * partsize) != SIZE_4KB)
		err(set_err(ECANCELED),
		    "Can't write %d b to '%s' p%d", SIZE_4KB, fname, p);
}

void
swap(int partnum)
{
	uint8_t *n = buf + (SIZE_4KB * partnum);

	for (size_t w = NVM_SIZE * ((uint8_t *) &e)[0], x = 1;
	    w < NVM_SIZE; w += 2, x += 2) {
		uint8_t chg = n[w];
		n[w] ^= n[x];
		n[x] ^= chg;
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
