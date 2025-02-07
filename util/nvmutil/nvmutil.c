/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2022-2025 Leah Rowe <leah@libreboot.org> */
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
    cmd_dump(void), cmd_setmac(void), readGbe(void), checkdir(const char *path),
    macf(int partnum), hexdump(int partnum), openFiles(const char *path),
    cmd_copy(void), parseMacString(const char *strMac, uint16_t *mac),
    cmd_swap(void);
int goodChecksum(int partnum);
uint8_t hextonum(char chs), rhex(void);

#define COMMAND argv[2]
#define MAC_ADDRESS argv[3]
#define PARTN argv[3]
#define NVM_CHECKSUM 0xBABA /* checksum value */
#define NVM_CHECKSUM_WORD 0x3F /* checksum word position */
#define NVM_SIZE 128 /* Area containing NVM words */

#define SIZE_4KB 0x1000
#define SIZE_8KB 0x2000
#define SIZE_16KB 0x4000
#define SIZE_128KB 0x20000

uint16_t mac[3] = {0, 0, 0};
ssize_t nf;
size_t partsize, gbe[2];
uint8_t nvmPartChanged[2] = {0, 0}, do_read[2] = {1, 1};
int flags, rfd, fd, part;

const char *strMac = NULL, *strRMac = "??:??:??:??:??:??", *filename = NULL;

/* available commands, set a pointer based on user command */
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

/* wrappers for BSD-style err() function (error handling) */
#define ERR() errno = errno ? errno : ECANCELED
#define err_if(x) if (x) err(ERR(), "%s", filename)

/* Macro for opening a file with errors properly handled */
#define xopen(f,l,p) if ((f = open(l, p)) == -1) err(ERR(), "%s", l); \
    if (fstat(f, &st) == -1) err(ERR(), "%s", l)

/* Macros for reading/writing the GbE file in memory */
#define word(pos16, partnum) ((uint16_t *) gbe[partnum])[pos16]
#define setWord(pos16, p, val16) if (word(pos16, p) != val16) \
    nvmPartChanged[p] = 1 | (word(pos16, p) = val16)

int
main(int argc, char *argv[])
{
#ifdef __OpenBSD__
	/* OpenBSD pledge (sandboxing): https://man.openbsd.org/pledge.2 */
	err_if(pledge("stdio rpath wpath unveil", NULL) == -1);
#endif

	if (argc < 2) { /* TODO: manpage! */
#ifdef __OpenBSD__
		err_if(pledge("stdio", NULL) == -1);
#endif
		fprintf(stderr, "Modify Intel GbE NVM images e.g. set MAC\n");
		fprintf(stderr, "USAGE:\n");
		fprintf(stderr, " %s FILE dump\n", argv[0]);
		fprintf(stderr, " %s FILE\n # same as setmac without arg\n",
		    argv[0]);
		fprintf(stderr, " %s FILE setmac [MAC]\n", argv[0]);
		fprintf(stderr, " %s FILE swap\n", argv[0]);
		fprintf(stderr, " %s FILE copy 0|1\n", argv[0]);
		fprintf(stderr, " %s FILE brick 0|1\n", argv[0]);
		fprintf(stderr, " %s FILE setchecksum 0|1\n", argv[0]);
		err(errno = ECANCELED, "Too few arguments");
	}

	filename = argv[1];

	flags = O_RDWR;

	if (argc > 2) {
		if (strcmp(COMMAND, "dump") == 0) {
			flags = O_RDONLY; /* write not needed for dump cmd */
#ifdef __OpenBSD__
			/* writes not needed for the dump command */
			err_if(pledge("stdio rpath unveil", NULL) == -1);
#endif
		}
	}

	/* check for dir first, to prevent unveil from
	   permitting directory access on OpenBSD */
	checkdir("/dev/urandom");
	checkdir(filename); /* Must be a file, not a directory */

#ifdef __OpenBSD__
	/* OpenBSD unveil: https://man.openbsd.org/unveil.2 */
	err_if(unveil("/dev/urandom", "r") == -1);

	/* Only allow access to /dev/urandom and the gbe file */
	if (flags == O_RDONLY) { /* dump command */
		err_if(unveil(filename, "r") == -1); /* write not needed */
		err_if(unveil(NULL, NULL) == -1); /* lock unveil */
		err_if(pledge("stdio rpath", NULL) == -1); /* lock unveil */
	} else { /* other commands need read-write */
		err_if(unveil(filename, "rw") == -1);
		err_if(unveil(NULL, NULL) == -1); /* lock unveil */
		err_if(pledge("stdio rpath wpath", NULL) == -1); /* no unveil */
	}
#endif

	openFiles(filename); /* open files first, to allow harder pledge: */
#ifdef __OpenBSD__
	/* OpenBSD sandboxing: https://man.openbsd.org/pledge.2 */
	err_if(pledge("stdio", NULL) == -1);
#endif

	if (argc > 2) {
		for (int i = 0; (i < 6) && (cmd == NULL); i++) {
			if (strcmp(COMMAND, op[i].str) != 0)
				continue;
			if (argc >= op[i].args) {
				cmd = op[i].cmd;
				break;
			}
			err(errno = EINVAL, "Too few args on command '%s'",
			    op[i].str);
		}
	} else {
		cmd = cmd_setmac;
	}

	if ((cmd == NULL) && (argc > 2)) { /* nvm gbe [MAC] */
		strMac = COMMAND;
		cmd = cmd_setmac;
	} else if (cmd == cmd_setmac) { /* nvm gbe setmac [MAC] */
		strMac = strRMac; /* random MAC */
		if (argc > 3) /* user-supplied MAC (can be random) */
			strMac = MAC_ADDRESS;
	} else if ((cmd != NULL) && (argc > 3)) { /* user-supplied partnum */
		err_if((errno = (!((part = PARTN[0] - '0') == 0 || part == 1))
		    || PARTN[1] ? EINVAL : errno)); /* only allow '0' or '1' */
	}
	err_if((errno = (cmd == NULL) ? EINVAL : errno)); /* bad user arg */

	readGbe(); /* read gbe file into memory */

	(*cmd)(); /* operate on gbe file in memory */

	writeGbe(); /* write changes back to file */

	err_if((errno != 0) && (cmd != cmd_dump)); /* don't err on dump */
	return errno; /* errno can be set by the dump command */
}

/*
 * check whether urandom/file is a directory, and err if so,
 * to prevent later unveil calls from permitting directory access
 * on OpenBSD
 */
void
checkdir(const char *path)
{
	if (opendir(path) != NULL)
		err(errno = EISDIR, "%s", path);
	if (errno == ENOTDIR)
		errno = 0;
	err_if(errno);
}

/* open gbe file and /dev/urandom, setting permissions */
void
openFiles(const char *path)
{
	struct stat st;

	xopen(fd, path, flags); /* gbe file */

	switch(st.st_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		partsize = st.st_size >> 1;
		break;
	default:
		err(errno = ECANCELED, "Invalid file size (not 8/16/128KiB)");
		break;
	}

	/* the MAC address randomiser relies on reading urandom */
	xopen(rfd, "/dev/urandom", O_RDONLY);
}

/* read gbe file into memory buffer */
void
readGbe(void)
{
	if ((cmd == cmd_swap) || (cmd == cmd_copy))
		nf = SIZE_4KB; /* read/write the entire block */
			/* only need to do 4KB even on larger gbe files */
	else
		nf = NVM_SIZE; /* only read/write the nvm part of the block */

	if ((cmd == cmd_copy) || (cmd == cmd_setchecksum) || (cmd == cmd_brick))
		do_read[part ^ 1] = 0; /* only read the user-specified part */

	/* AND do_read[*] to avoid wasteful malloc */
	/* cmd_copy also relies on this */
	char *buf = malloc(nf << (do_read[0] & do_read[1]));
	if (buf == NULL)
		err(errno, NULL);

	/* we pread per-part, so each part has its own pointer: */
	/* if a do_read is 0, both pointers are the same; this accomplishes
	   the desired result for cmd_copy (see cmd_copy function) */
	gbe[0] = (size_t) buf;
	gbe[1] = gbe[0] + (nf * (do_read[0] & do_read[1]));

	ssize_t tnr = 0; /* total bytes read */

	for (int p = 0; p < 2; p++) {
		if (!do_read[p])
			continue; /* avoid unnecessary reads */

		ssize_t nr = pread(fd, (uint8_t *) gbe[p], nf, p * partsize);
		err_if(nr == -1);
		if (nr != nf)
			err(errno == ECANCELED,
			    "%ld bytes written on '%s', expected %ld bytes\n",
			    nr, filename, nf);

		tnr += nr;

		swap(p); /* handle big-endian host CPU */
	}

	printf("%ld bytes read from file '%s'\n", tnr, filename);
}

/* set MAC address and checksum on nvm part */
void
cmd_setmac(void)
{
	int mac_updated = 0;
	parseMacString(strMac, mac);

	printf("MAC address to be written: %s\n", strMac);

	for (int partnum = 0; partnum < 2; partnum++) {
		if (!goodChecksum(part = partnum))
			continue;

		for (int w = 0; w < 3; w++) /* write MAC to gbe part */
			setWord(w, partnum, mac[w]);

		printf("Wrote MAC address to part %d: ", partnum);
		macf(partnum);

		cmd_setchecksum(); /* MAC updated; need valid checksum */
		mac_updated = 1;
	}

	if (mac_updated)
		errno = 0; /* reset in case one of the checksums failed */
}

/* parse MAC string, write to char buffer */
void
parseMacString(const char *strMac, uint16_t *mac)
{
	uint64_t total = 0;
	if (strnlen(strMac, 20) != 17)
		err(errno = EINVAL, "Invalid MAC address string length");

	for (uint8_t h, i = 0; i < 16; i += 3) {
		if (i != 15)
			if (strMac[i + 2] != ':')
				err(errno = EINVAL,
				    "Invalid MAC address separator '%c'",
				    strMac[i + 2]);

		int byte = i / 3;

		/* Update MAC buffer per-nibble from a given string */
		for (int nib = 0; nib < 2; nib++, total += h) {
			if ((h = hextonum(strMac[i + nib])) > 15)
				err(errno = EINVAL, "Invalid character '%c'",
				    strMac[i + nib]);

			/* if random: ensure local-only, unicast MAC */
			if ((byte == 0) && (nib == 1)) /* unicast/local nib */
				if (strMac[i + nib] == '?') /* ?=random */
					h = (h & 0xE) | 2; /* local, unicast */

			mac[byte >> 1] |= ((uint16_t ) h)
			    << ((8 * (byte % 2)) + (4 * (nib ^ 1)));
		}
	}

	if (total == 0)
		err(errno = EINVAL, "Invalid MAC (all-zero MAC address)");
	if (mac[0] & 1)
		err(errno = EINVAL, "Invalid MAC (multicast bit set)");
}

/* convert hex char to char value (0-15) */
uint8_t
hextonum(char ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	else if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	else if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	return (ch == '?') ? rhex() : 16; /* 16 for error (invalid char) */
}

/* random number generator */
uint8_t
rhex(void)
{
	static uint8_t n = 0, rnum[16];
	if (!n)
		err_if(pread(rfd, (uint8_t *) &rnum, (n = 15) + 1, 0) == -1);
	return rnum[n--] & 0xf;
}

/* print mac address and hexdump of parts */
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

/* print mac address of part */
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

/* print hexdump of nvm part */
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

/* correct the checksum on part */
void
cmd_setchecksum(void)
{
	uint16_t val16 = 0;
	for (int c = 0; c < NVM_CHECKSUM_WORD; c++)
		val16 += word(c, part);

	/* correct the checksum */
	setWord(NVM_CHECKSUM_WORD, part, NVM_CHECKSUM - val16);
}

/* intentionally set wrong checksum on part */
void
cmd_brick(void)
{
	if (goodChecksum(part))
		setWord(NVM_CHECKSUM_WORD, part,
		    ((word(NVM_CHECKSUM_WORD, part)) ^ 0xFF));
}

/* overwrite the contents of one part with the other */
void
cmd_copy(void)
{
	nvmPartChanged[part ^ 1] = goodChecksum(part);

	/* no need to actually copy because gbe[] pointers are both the same */
	/* we simply set the right nvm part as changed, and write the file */
}

/* swap contents between the two parts */
void
cmd_swap(void) {
	err_if(!(goodChecksum(0) || goodChecksum(1)));
	errno = 0;

	/* speedhack: swap pointers, not words. (xor swap) */
	gbe[0] ^= gbe[1];
	gbe[1] ^= gbe[0];
	gbe[0] ^= gbe[1];

	nvmPartChanged[0] = nvmPartChanged[1] = 1;
}

/* verify nvm part checksum (return 1 if valid) */
int
goodChecksum(int partnum)
{
	uint16_t total = 0;
	for(int w = 0; w <= NVM_CHECKSUM_WORD; w++)
		total += word(w, partnum);

	if (total == NVM_CHECKSUM)
		return 1;

	fprintf(stderr, "WARNING: BAD checksum in part %d\n", partnum);
	errno = ECANCELED;
	return 0;
}

/* write the nvm parts back to the file */
void
writeGbe(void)
{
	ssize_t tnw = 0; /* total bytes written */

	for (int p = 0; p < 2; p++) {
		if ((!nvmPartChanged[p]) || (flags == O_RDONLY))
			continue;

		swap(p); /* swap bytes on big-endian host CPUs */
		ssize_t nw = pwrite(fd, (uint8_t *) gbe[p], nf, p * partsize);
		err_if(nw == -1);
		if (nw != nf)
			err(errno == ECANCELED,
			    "%ld bytes written on '%s', expected %ld bytes\n",
			    nw, filename, nf);

		tnw += nf;
	}

	if ((flags != O_RDONLY) && (cmd != cmd_dump)) {
		if (nvmPartChanged[0] || nvmPartChanged[1])
			printf("The following nvm words were written:\n");
		cmd_dump();
	}

	if ((!tnw) && (flags != O_RDONLY) && (!errno))
		fprintf(stderr, "No changes needed on file '%s'\n", filename);
	else if (tnw)
		printf("%ld bytes written to file '%s'\n", tnw, filename);

	if (tnw)
		errno = 0;

	err_if(close(fd) == -1);
}

/* swap byte order on big-endian CPUs. swap skipped on little endian */
void
swap(int partnum) /* swaps bytes in words, not pointers. */
{		/* not to be confused with cmd_swap */
	size_t w, x;
	uint8_t *n = (uint8_t *) gbe[partnum];
	int e = 1;

	for (w = NVM_SIZE * ((uint8_t *) &e)[0], x = 1; w < NVM_SIZE;
	    w += 2, x += 2) {
		n[w] ^= n[x];
		n[x] ^= n[w];
		n[w] ^= n[x];
	}
}
