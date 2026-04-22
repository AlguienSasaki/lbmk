/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org> */
/* Copyright (c) 2023 Riku Viitanen <riku.viitanen@protonmail.com> */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>

void cmd_setchecksum(void), cmd_brick(void), swap(int partnum), writeGbe(void),
    cmd_dump(void), cmd_setmac(void), readGbe(void),
    macf(int partnum), hexdump(int partnum), openFiles(const char *path),
    cmd_copy(void), parseMacString(const char *strMac, uint16_t *mac),
    cmd_swap(void), xclose(int *fd);
int goodChecksum(int partnum), open_on_eintr(const char *pathname, int flags),
    fs_retry(int saved_errno, int rval),
    rw_retry(int saved_errno, ssize_t rval), if_err(int condition, int errval),
    if_err_sys(int condition);
ssize_t rw_exact(int fd, unsigned char *mem, size_t nrw,
    off_t off, int rw_type);
ssize_t rw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type);
int io_args(int fd, void *mem, size_t nrw,
    off_t off, int rw_type);
int with_fallback_errno(int fallback);
ssize_t rw_over_nrw(ssize_t r, size_t nrw);
uint8_t hextonum(char chs), rhex(void);

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

#define IO_READ 0
#define IO_WRITE 1
#define IO_PREAD 2
#define IO_PWRITE 3

uint16_t mac[3] = {0, 0, 0};
ssize_t nf;
size_t partsize;
uintptr_t gbe[2];
uint8_t nvmPartChanged[2] = {0, 0}, do_read[2] = {1, 1};
int flags, rfd, fd, part;

const char *strMac = NULL, *strRMac = "xx:xx:xx:xx:xx:xx", *filename = NULL;

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

#define err_if(x) if (x) err(EXIT_FAILURE, "%s", filename)

#define xopen(f,l,p) \
	do { \
	if ((f = open_on_eintr(l, p)) == -1) \
		err(EXIT_FAILURE, "%s", l); \
	if (fstat(f, &st) == -1) \
		err(EXIT_FAILURE, "%s", l); \
	} while(0)

#define word(pos16, partnum) ((uint16_t *) gbe[partnum])[pos16]
#define setWord(pos16, p, val16) if (word(pos16, p) != val16) \
    nvmPartChanged[p] = 1 | (word(pos16, p) = val16)

#define SUCCESS(x) ((x) >= 0)

#define reset_caller_errno(return_value) \
	do { \
	if (SUCCESS(return_value) && (!errno)) \
		errno = saved_errno; \
	} while (0)

int
main(int argc, char *argv[])
{
#ifdef __OpenBSD__
	err_if(pledge("stdio rpath wpath unveil", NULL) == -1);
#endif

	if (argc < 2) {
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
		errno = EINVAL;
		err(EXIT_FAILURE, "Too few arguments");
	}

	filename = argv[1];

	flags = O_RDWR;

	if (argc > 2) {
		if (strcmp(COMMAND, "dump") == 0) {
			flags = O_RDONLY;
#ifdef __OpenBSD__
			err_if(pledge("stdio rpath unveil", NULL) == -1);
#endif
		}
	}

#ifdef __OpenBSD__
	err_if(unveil("/dev/urandom", "r") == -1);

	if (flags == O_RDONLY) {
		err_if(unveil(filename, "r") == -1);
		err_if(unveil(NULL, NULL) == -1);
		err_if(pledge("stdio rpath", NULL) == -1);
	} else {
		err_if(unveil(filename, "rw") == -1);
		err_if(unveil(NULL, NULL) == -1);
		err_if(pledge("stdio rpath wpath", NULL) == -1);
	}
#endif

	openFiles(filename);
#ifdef __OpenBSD__
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
			errno = EINVAL;
			err(EXIT_FAILURE, "Too few args on command '%s'",
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
		if (argc > 3)
			strMac = MAC_ADDRESS;
	} else if ((cmd != NULL) && (argc > 3)) { /* user-supplied partnum */
		err_if((errno = (!((part = PARTN[0] - '0') == 0 || part == 1))
		    || PARTN[1] ? EINVAL : 0)); /* only allow '0' or '1' */
	}
	if (cmd == NULL) {
		errno = EINVAL;
		err(EXIT_FAILURE, "Bad command");
	}

	readGbe();
	(*cmd)();
	writeGbe();

	return EXIT_SUCCESS;
}

void
openFiles(const char *path)
{
	struct stat st;

	xopen(rfd, "/dev/urandom", O_RDONLY);	
	xopen(fd, path, flags);

	switch(st.st_size) {
	case SIZE_8KB:
	case SIZE_16KB:
	case SIZE_128KB:
		partsize = st.st_size >> 1;
		break;
	default:
		errno = ECANCELED;
		err(EXIT_FAILURE, "Invalid file size (not 8/16/128KiB)");
		break;
	}

	if (if_err(!S_ISREG(st.st_mode), EBADF))
		err(EXIT_FAILURE, "Not a GbE file");	
}

void
readGbe(void)
{
	if ((cmd == cmd_swap) || (cmd == cmd_copy))
		nf = SIZE_4KB;
	else
		nf = NVM_SIZE;

	if ((cmd == cmd_copy) || (cmd == cmd_setchecksum) || (cmd == cmd_brick))
		do_read[part ^ 1] = 0;

	char *buf = malloc(nf << (do_read[0] & do_read[1]));
	if (buf == NULL)
		err(EXIT_FAILURE, "malloc");

	gbe[0] = (uintptr_t) buf;
	gbe[1] = gbe[0] + (nf * (do_read[0] & do_read[1]));

	ssize_t tnr = 0;

	for (int p = 0; p < 2; p++) {
		if (!do_read[p])
			continue;

		ssize_t nr = rw_exact(fd, (uint8_t *) gbe[p],
		    nf, p * partsize, IO_PREAD);
		err_if(nr == -1);
		if (nr != nf)
			err(EXIT_FAILURE,
			    "%ld bytes read from '%s', expected %ld bytes\n",
			    nr, filename, nf);

		tnr += nr;
		swap(p); /* handle big-endian host CPU */
	}

	printf("%ld bytes read from file '%s'\n", tnr, filename);
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
		return;

	errno = EINVAL;
	err(EXIT_FAILURE, "Error updating MAC address");
}

void
parseMacString(const char *strMac, uint16_t *mac)
{
	uint64_t total = 0;
	if (strnlen(strMac, 20) != 17) {
		errno = EINVAL;
		err(EXIT_FAILURE, "Invalid MAC address string length");
	}

	for (uint8_t h, i = 0; i < 16; i += 3) {
		if (i != 15)
			if (strMac[i + 2] != ':') {
				errno = EINVAL;
				err(EXIT_FAILURE,
				    "Invalid MAC address separator '%c'",
				    strMac[i + 2]);
			}

		int byte = i / 3;

		for (int nib = 0; nib < 2; nib++, total += h) {
			if ((h = hextonum(strMac[i + nib])) > 15) {
				errno = EINVAL;
				err(EXIT_FAILURE, "Invalid character '%c'",
				    strMac[i + nib]);
			}

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

	if (!((total == 0) || (mac[0] & 1)))
		return;

	errno = EINVAL;

	if (total == 0)
		err(EXIT_FAILURE, "Invalid MAC (all-zero MAC address)");
	if (mac[0] & 1)
		err(EXIT_FAILURE, "Invalid MAC (multicast bit set)");
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
	else
		return 16; /* error: invalid character */
}

uint8_t
rhex(void)
{
	static uint8_t n = 0, rnum[16];
	if (!n)
		err_if(rw_exact(rfd, (uint8_t *) &rnum,
		    (n = 15) + 1, 0, IO_READ) == -1);
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
	if(!(goodChecksum(0) || goodChecksum(1))) {
		errno = EINVAL;
		err(EXIT_FAILURE, "Invalid checksums");
	}

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
	return 0;
}

void
writeGbe(void)
{
	ssize_t tnw = 0;

	for (int p = 0; p < 2; p++) {
		if ((!nvmPartChanged[p]) || (flags == O_RDONLY))
			continue;

		swap(p); /* swap bytes on big-endian host CPUs */
		ssize_t nw = rw_exact(fd, (uint8_t *) gbe[p], nf,
		    p * partsize, IO_PWRITE);
		err_if(nw == -1);
		if (nw != nf) {
			errno = ECANCELED;
			err(EXIT_FAILURE,
			    "%ld bytes written to '%s', expected %ld bytes\n",
			    nw, filename, nf);
		}

		tnw += nf;
	}

	if ((flags != O_RDONLY) && (cmd != cmd_dump)) {
		if (nvmPartChanged[0] || nvmPartChanged[1])
			printf("The following nvm words were written:\n");
		cmd_dump();
	}

	if ((!tnw) && (flags != O_RDONLY))
		fprintf(stderr, "No changes needed on file '%s'\n", filename);
	else if (tnw)
		printf("%ld bytes written to file '%s'\n", tnw, filename);

	xclose(&fd);
}

void
xclose(int *fd)
{
	int saved_errno = errno;
	int rval = 0;

	if (fd == NULL)
		err(EXIT_FAILURE, "xclose: null pointer");
	if (*fd < 0)
		return;

	errno = 0;
	if ((rval = close(*fd)) < 0) {
		if (errno != EINTR)
			err(EXIT_FAILURE, "xclose: could not close");

		/* regard EINTR as a successful close */
		rval = 0;
	}

	*fd = -1;

	reset_caller_errno(rval);
}

void
swap(int partnum)
{
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

int
open_on_eintr(const char *pathname,
    int flags)
{
	int saved_errno = errno;
	int rval = 0;
	errno = 0;

	while (fs_retry(saved_errno,
	    rval = open(pathname, flags)));

	reset_caller_errno(rval);
	return rval;
}


ssize_t
rw_exact(int fd, unsigned char *mem, size_t nrw,
    off_t off, int rw_type)
{
	int saved_errno = errno;
	ssize_t rval = 0;
	ssize_t rc = 0;
	size_t nrw_cur;
	off_t off_cur;
	void *mem_cur;
	errno = 0;

	if (io_args(fd, mem, nrw, off, rw_type) == -1)
		goto err_rw_exact;

	while (1) {
	
		/* Prevent theoretical overflow */
		if (if_err(rval >= 0 && (size_t)rval > (nrw - (size_t)rc),
		    EOVERFLOW))
			goto err_rw_exact;

		rc += rval;
		if ((size_t)rc >= nrw)
			break;

		mem_cur = (void *)(mem + (size_t)rc);
		nrw_cur = (size_t)(nrw - (size_t)rc);

		if (if_err(off < 0, EOVERFLOW))
			goto err_rw_exact;

		off_cur = off + (off_t)rc;

		if ((rval = rw(fd, mem_cur, nrw_cur, off_cur, rw_type)) <= 0)
			goto err_rw_exact;
	}

	if (if_err((size_t)rc != nrw, EIO) ||
	    (rval = rw_over_nrw(rc, nrw)) < 0)
		goto err_rw_exact;

	reset_caller_errno(rval);
	return rval;

err_rw_exact:
	return with_fallback_errno(EIO);
}

ssize_t
rw(int fd, void *mem, size_t nrw,
    off_t off, int rw_type)
{
	ssize_t rval = 0;
	ssize_t r = -1;
	int saved_errno = errno;
	errno = 0;

	if (io_args(fd, mem, nrw, off, rw_type) == -1 ||
	    if_err(mem == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(off < 0, EFAULT) ||
	    if_err(nrw == 0, EINVAL))
		return with_fallback_errno(EIO);

	do {
		switch (rw_type) {
		case IO_READ:
			r = read(fd, mem, nrw);
			break;
		case IO_WRITE:
			r = write(fd, mem, nrw);
			break;
		case IO_PREAD:
			r = pread(fd, mem, nrw, off);
			break;
		case IO_PWRITE:
			r = pwrite(fd, mem, nrw, off);
			break;
		default:
			errno = EINVAL;
			break;
		}

	} while (rw_retry(saved_errno, r));
	
	if ((rval = rw_over_nrw(r, nrw)) < 0)
		return with_fallback_errno(EIO);

	reset_caller_errno(rval);
	return rval;
}

int
io_args(int fd, void *mem, size_t nrw,
    off_t off, int rw_type)
{
	int saved_errno = errno;
	errno = 0;

	if (if_err(mem == NULL, EFAULT) ||
	    if_err(fd < 0, EBADF) ||
	    if_err(off < 0, ERANGE) ||
	    if_err(!nrw, EPERM) || /* TODO: toggle zero-byte check */
	    if_err(nrw > (size_t)SSIZE_MAX, ERANGE) ||
	    if_err(((size_t)off + nrw) < (size_t)off, ERANGE) ||
	    if_err(rw_type > IO_PWRITE, EINVAL))
		goto err_io_args;

	reset_caller_errno(0);
	return 0;

err_io_args:
	return with_fallback_errno(EINVAL);
}

ssize_t
rw_over_nrw(ssize_t r, size_t nrw)
{
	if (if_err(!nrw, EIO) ||
	    (r == -1) ||
	    if_err((size_t)r > SSIZE_MAX, ERANGE) ||
	    if_err((size_t)r > nrw, ERANGE))
		return with_fallback_errno(EIO);

	return r;
}

int
with_fallback_errno(int fallback)
{
	if (!errno)
		errno = fallback;
	return -1;
}

/* two functions that reduce sloccount by
 * two hundred lines */
int
if_err(int condition, int errval)
{
	if (!condition)
		return 0;
	if (errval)
		errno = errval;
	return 1;
}
int
if_err_sys(int condition)
{
	if (!condition)
		return 0;
	return 1;
}

#define fs_err_retry() \
	do { \
		if ((rval == -1) && \
		    (errno == EINTR)) \
			return 1; \
		if (rval >= 0 && !errno) \
			errno = saved_errno; \
		return 0; \
	} while(0)

int
fs_retry(int saved_errno, int rval)
{
	fs_err_retry();
}

int
rw_retry(int saved_errno, ssize_t rval)
{
	fs_err_retry();
}
