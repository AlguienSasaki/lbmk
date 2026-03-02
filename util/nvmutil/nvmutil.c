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
    cmd_dump(void), cmd_setmac(void), nvmalloc(void), readGbe(void),
    checkdir(const char *path), macf(int partnum), hexdump(int partnum),
    parseMacString(const char *strMac, uint16_t *mac), cmd_swap(void),
    openFiles(const char *path), cmd_copy(void), writeGbe_part(int),
    readGbe_part(int), usage(char*), set_io_flags(int, char **),
    set_cmd(int, char **);
int goodChecksum(int partnum);
uint8_t hextonum(char chs), rhex(void);

#ifdef __OpenBSD__
void block_unveil(void);
#endif

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

uint16_t mac[3] = {0, 0, 0};
ssize_t nf;
size_t partsize, gbe[2];
uint8_t nvmPartChanged[2] = {0, 0}, do_read[2] = {1, 1};
int flags, rfd, fd, part, e = 1;
struct stat st;

const char *strMac = NULL, *strRMac = "xx:xx:xx:xx:xx:xx", *fname = NULL;

typedef struct op {
	char *str;
	void (*cmd)(void);
	int args;
} op_t;
op_t op[] = {
{ .str = "dump", .cmd = cmd_dump, .args = 3},
{ .str = "setmac", .cmd = cmd_setmac, .args = 3},
{ .str = "swap", .cmd = cmd_swap, .args = 3},
{ .str = "copy", .cmd = cmd_copy, .args = 4},
{ .str = "brick", .cmd = cmd_brick, .args = 4},
{ .str = "setchecksum", .cmd = cmd_setchecksum, .args = 4},
};
void (*cmd)(void) = NULL;

#define SET_ERR(x) errno = errno ? errno : x
#define err_if(x) if (x) err(SET_ERR(ECANCELED), "%s", fname)

#define xopen(f,l,p) \
	if ((f = open(l, p)) == -1) \
	    err(SET_ERR(ECANCELED), "%s", l); \
	if (fstat(f, &st) == -1) err(SET_ERR(ECANCELED), "%s", l)

#define word(pos16, partnum) ((uint16_t *) gbe[partnum])[pos16]
#define setWord(pos16, p, val16) if (word(pos16, p) != val16) \
    nvmPartChanged[p] = 1 | (word(pos16, p) = val16)

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
	block_unveil();
#endif
	openFiles(fname);
#ifdef __OpenBSD__
	err_if(pledge("stdio", NULL) == -1);
#endif

	nvmalloc();
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
			err(SET_ERR(EINVAL), "Too few args on command '%s'",
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
		err(SET_ERR(EISDIR), "%s", path);
	if (errno == ENOTDIR)
		errno = 0;
	err_if(errno);
}

void
openFiles(const char *path)
{
	checkdir("/dev/urandom");
	checkdir(fname);

	xopen(rfd, "/dev/urandom", O_RDONLY);
	xopen(fd, path, flags);

	switch(st.st_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		partsize = st.st_size >> 1;
		break;
	default:
		err(SET_ERR(ECANCELED), "Invalid file size (not 8/16/128KiB)");
		break;
	}
}

void
nvmalloc(void)
{
	/* same operations need the full block, others only 128 bytes */
	nf = NVM_SIZE;
	if ((cmd == cmd_swap) || (cmd == cmd_copy))
		nf = SIZE_4KB;

	/* only read the part specified, for copy/setchecksum/brick */
	if ((cmd == cmd_copy) || (cmd == cmd_setchecksum) || (cmd == cmd_brick))
		do_read[part ^ 1] = 0;

	/* only allocate one block if only one part being read */
	char *buf = malloc(nf << (do_read[0] & do_read[1]));
	if (buf == NULL)
		err(errno, NULL);

	gbe[0] = (size_t) buf;

	/* speedhack: for cmd copy, both pointers are set the same */
	gbe[1] = gbe[0] + (nf * (do_read[0] & do_read[1]));
}

void
readGbe(void)
{
	for (int p = 0; p < 2; p++)
		if (do_read[p])
			readGbe_part(p);
}

void
readGbe_part(int p)
{
	if (pread(fd, (uint8_t *) gbe[p], nf, p * partsize) != nf)
		err(SET_ERR(ECANCELED),
		    "Can't read %ld b from '%s' p%d", nf, fname, p);
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
		err(SET_ERR(EINVAL), "Invalid MAC address string length");

	for (uint8_t h, i = 0; i < 16; i += 3) {
		if (i != 15)
			if (strMac[i + 2] != ':')
				err(SET_ERR(EINVAL),
				    "Invalid MAC address separator '%c'",
				    strMac[i + 2]);

		int byte = i / 3;

		for (int nib = 0; nib < 2; nib++, total += h) {
			if ((h = hextonum(strMac[i + nib])) > 15)
				err(SET_ERR(EINVAL), "Invalid character '%c'",
				    strMac[i + nib]);

			/* If random, ensure that local/unicast bits are set */
			if ((byte == 0) && (nib == 1))
				if ((strMac[i + nib] == '?') ||
				    (strMac[i + nib] == 'x') ||
				    (strMac[i + nib] == 'X')) /* random */
					h = (h & 0xE) | 2; /* local, unicast */

			mac[byte >> 1] |= ((uint16_t ) h)
			    << ((8 * (byte % 2)) + (4 * (nib ^ 1)));
		}
	}

	if (total == 0)
		err(SET_ERR(EINVAL), "Invalid MAC (all-zero MAC address)");
	if (mac[0] & 1)
		err(SET_ERR(EINVAL), "Invalid MAC (multicast bit set)");
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
		if ((cmd != cmd_dump) && (flags != O_RDONLY) &&
		    (!nvmPartChanged[partnum]))
			continue;

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
	nvmPartChanged[part ^ 1] = goodChecksum(part);
}

void
cmd_swap(void) {
	err_if(!(goodChecksum(0) || goodChecksum(1)));
	errno = 0;

	gbe[0] ^= gbe[1];
	gbe[1] ^= gbe[0];
	gbe[0] ^= gbe[1];

	nvmPartChanged[0] = nvmPartChanged[1] = 1;
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
	SET_ERR(ECANCELED);
	return 0;
}

void
writeGbe(void)
{
	for (int p = 0; p < 2; p++)
		if ((nvmPartChanged[p]) && (flags != O_RDONLY))
			writeGbe_part(p);
	err_if(close(fd) == -1);
}

void
writeGbe_part(int p)
{
	swap(p); /* swap bytes on big-endian host CPUs */
	if(pwrite(fd, (uint8_t *) gbe[p], nf, p * partsize) != nf)
		err(SET_ERR(ECANCELED),
		    "Can't write %ld b to '%s' p%d", nf, fname, p);

}

void
swap(int partnum)
{
	uint8_t *n = (uint8_t *) gbe[partnum];

	for (size_t w = NVM_SIZE * ((uint8_t *) &e)[0], x = 1;
	    w < NVM_SIZE; w += 2, x += 2) {
		n[w] ^= n[x];
		n[x] ^= n[w];
		n[w] ^= n[x];
	}
}

#ifdef __OpenBSD__
void
block_unveil(void)
{
	if (flags == O_RDONLY) {
		err_if(unveil(fname, "r") == -1);
		err_if(unveil(NULL, NULL) == -1);
		err_if(pledge("stdio rpath", NULL) == -1);
	} else {
		err_if(unveil(fname, "rw") == -1);
		err_if(unveil(NULL, NULL) == -1);
		err_if(pledge("stdio rpath wpath", NULL) == -1);
	}
}
#endif

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
	err(SET_ERR(ECANCELED), "Too few arguments");
}
