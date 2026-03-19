/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
 *
 * Command handlers for nvmutil
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "../include/common.h"

/* Guard against regressions by maintainers (command table)
 */

void
sanitize_command_list(void)
{
	struct xstate *x = xstatus(0, NULL);

	unsigned long c;
	unsigned long num_commands;

	num_commands = items(x->cmd);

	for (c = 0; c < num_commands; c++)
		sanitize_command_index(c);
}

/* TODO: specific config checks per command
 */

void
sanitize_command_index(unsigned long c)
{
	struct xstate *x = xstatus(0, NULL);
	struct commands *cmd = &x->cmd[c];

	int _flag;
	unsigned long gbe_rw_size;

	check_command_num(c);

	if (cmd->argc < 3)
		err(EINVAL, "cmd index %lu: argc below 3, %d",
		    (unsigned long)c, cmd->argc);

	if (cmd->str == NULL)
		err(EINVAL, "cmd index %lu: NULL str",
		    (unsigned long)c);

	if (*cmd->str == '\0')
		err(EINVAL, "cmd index %lu: empty str",
		    (unsigned long)c);

	if (xstrxlen(cmd->str, MAX_CMD_LEN + 1) >
	    MAX_CMD_LEN) {
		err(EINVAL, "cmd index %lu: str too long: %s",
		    (unsigned long)c, cmd->str);
	}

	if (cmd->run == NULL)
		err(EINVAL, "cmd index %lu: cmd ptr null",
		    (unsigned long)c);

	check_bin(cmd->arg_part, "cmd.arg_part");
	check_bin(cmd->chksum_read, "cmd.chksum_read");
	check_bin(cmd->chksum_write, "cmd.chksum_write");

	gbe_rw_size = cmd->rw_size;

	switch (gbe_rw_size) {
	case GBE_PART_SIZE:
	case NVM_SIZE:
		break;
	default:
		err(EINVAL, "Unsupported rw_size: %lu",
		    (unsigned long)gbe_rw_size);
	}

	if (gbe_rw_size > GBE_PART_SIZE)
		err(EINVAL, "rw_size larger than GbE part: %lu",
		    (unsigned long)gbe_rw_size);

	_flag = (cmd->flags & O_ACCMODE);

	if (_flag != O_RDONLY &&
	    _flag != O_RDWR)
		err(EINVAL, "invalid cmd.flags setting");
}

void
set_cmd(int argc, char *argv[])
{
	struct xstate *x = xstatus(0, NULL);
	const char *cmd;

	unsigned long c;

	for (c = 0; c < items(x->cmd); c++) {

		cmd = x->cmd[c].str;

		/* not the right command */
		if (xstrxcmp(argv[2], cmd, MAX_CMD_LEN) != 0)
			continue;

		/* valid command found */
		if (argc >= x->cmd[c].argc) {
			x->no_cmd = 0;
			x->i = c; /* set command */

			return;
		}

		err(EINVAL,
		    "Too few args on command '%s'", cmd);
	}

	x->no_cmd = 1;
}

void
set_cmd_args(int argc, char *argv[])
{
	struct xstate *x = xstatus(0, NULL);
	unsigned long i = x->i;
	struct commands *cmd = &x->cmd[i];
	struct xfile *f = &x->f;

	if (!valid_command(i) || argc < 3)
		usage();

	if (x->no_cmd)
		usage();

	/* Maintainer bugs */
	if (cmd->arg_part && argc < 4)
		err(EINVAL,
		    "arg_part set for command that needs argc4");

	if (cmd->arg_part && i == CMD_SETMAC)
		err(EINVAL,
		    "arg_part set on CMD_SETMAC");

	if (i == CMD_SETMAC) {

		if (argc >= 4)
			x->mac.str = argv[3];
		else
			x->mac.str = x->mac.rmac;

	} else if (cmd->arg_part) {

		f->part = conv_argv_part_num(argv[3]);
	}
}

unsigned long
conv_argv_part_num(const char *part_str)
{
	unsigned char ch;

	if (part_str[0] == '\0' || part_str[1] != '\0')
		err(EINVAL, "Partnum string '%s' wrong length", part_str);

	/* char signedness is implementation-defined */
	ch = (unsigned char)part_str[0];
	if (ch < '0' || ch > '1')
		err(EINVAL, "Bad part number (%c)", ch);

	return (unsigned long)(ch - '0');
}

void
check_command_num(unsigned long c)
{
	if (!valid_command(c))
		err(EINVAL, "Invalid run_cmd arg: %lu",
		    (unsigned long)c);
}

unsigned char
valid_command(unsigned long c)
{
	struct xstate *x = xstatus(0, NULL);
	struct commands *cmd;

	if (c >= items(x->cmd))
		return 0;

	cmd = &x->cmd[c];

	if (c != cmd->chk)
		err(EINVAL,
		    "Invalid cmd chk value (%lu) vs arg: %lu",
		    cmd->chk, c);

	return 1;
}

void
cmd_helper_setmac(void)
{
	struct xstate *x = xstatus(0, NULL);
	struct macaddr *mac = &x->mac;

	unsigned long partnum;

	check_cmd(cmd_helper_setmac, "setmac");

	printf("MAC address to be written: %s\n", mac->str);
	parse_mac_string();

	for (partnum = 0; partnum < 2; partnum++)
		write_mac_part(partnum);
}

void
parse_mac_string(void)
{
	struct xstate *x = xstatus(0, NULL);
	struct macaddr *mac = &x->mac;

	unsigned long mac_byte;

	if (xstrxlen(x->mac.str, 18) != 17)
		err(EINVAL, "MAC address is the wrong length");

	memset(mac->mac_buf, 0, sizeof(mac->mac_buf));

	for (mac_byte = 0; mac_byte < 6; mac_byte++)
		set_mac_byte(mac_byte);

	if ((mac->mac_buf[0] | mac->mac_buf[1] | mac->mac_buf[2]) == 0)
		err(EINVAL, "Must not specify all-zeroes MAC address");

	if (mac->mac_buf[0] & 1)
		err(EINVAL, "Must not specify multicast MAC address");
}

void
set_mac_byte(unsigned long mac_byte_pos)
{
	struct xstate *x = xstatus(0, NULL);
	struct macaddr *mac = &x->mac;

	char separator;

	unsigned long mac_str_pos;
	unsigned long mac_nib_pos;

	mac_str_pos = mac_byte_pos * 3;

	if (mac_str_pos < 15) {
		if ((separator = mac->str[mac_str_pos + 2]) != ':')
			err(EINVAL, "Invalid MAC address separator '%c'",
			    separator);
	}

	for (mac_nib_pos = 0; mac_nib_pos < 2; mac_nib_pos++)
		set_mac_nib(mac_str_pos, mac_byte_pos, mac_nib_pos);
}

void
set_mac_nib(unsigned long mac_str_pos,
    unsigned long mac_byte_pos, unsigned long mac_nib_pos)
{
	struct xstate *x = xstatus(0, NULL);
	struct macaddr *mac = &x->mac;

	char mac_ch;
	unsigned short hex_num;

	mac_ch = mac->str[mac_str_pos + mac_nib_pos];

	if ((hex_num = hextonum(mac_ch)) > 15)
		err(EINVAL, "Invalid character '%c'",
		    mac->str[mac_str_pos + mac_nib_pos]);

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
	mac->mac_buf[mac_byte_pos >> 1] |= hex_num <<
	    (((mac_byte_pos & 1) << 3) /* left or right byte? */
	    | ((mac_nib_pos ^ 1) << 2)); /* left or right nib? */
}

void
write_mac_part(unsigned long partnum)
{
	struct xstate *x = xstatus(0, NULL);
	struct xfile *f = &x->f;
	struct macaddr *mac = &x->mac;

	unsigned long w;

	check_bin(partnum, "part number");
	if (!f->part_valid[partnum])
		return;

	for (w = 0; w < 3; w++)
		set_nvm_word(w, partnum, mac->mac_buf[w]);

	printf("Wrote MAC address to part %lu: ",
	    (unsigned long)partnum);
	print_mac_from_nvm(partnum);
}

void
cmd_helper_dump(void)
{
	struct xstate *x = xstatus(0, NULL);
	struct xfile *f = &x->f;

	unsigned long p;

	check_cmd(cmd_helper_dump, "dump");

	f->part_valid[0] = good_checksum(0);
	f->part_valid[1] = good_checksum(1);

	for (p = 0; p < 2; p++) {

		if (!f->part_valid[p]) {

			fprintf(stderr,
			    "BAD checksum %04x in part %lu (expected %04x)\n",
			    nvm_word(NVM_CHECKSUM_WORD, p),
			    (unsigned long)p,
			    calculated_checksum(p));
		}

		printf("MAC (part %lu): ",
		    (unsigned long)p);

		print_mac_from_nvm(p);

		hexdump(p);
	}
}

void
print_mac_from_nvm(unsigned long partnum)
{
	unsigned long c;
	unsigned short val16;

	for (c = 0; c < 3; c++) {

		val16 = nvm_word(c, partnum);

		printf("%02x:%02x",
		    (unsigned int)(val16 & 0xff),
		    (unsigned int)(val16 >> 8));

		if (c == 2)
			printf("\n");
		else
			printf(":");
	}
}

void
hexdump(unsigned long partnum)
{
	unsigned long c;
	unsigned long row;
	unsigned short val16;

	for (row = 0; row < 8; row++) {

		printf("%08lx ",
		    (unsigned long)((unsigned long)row << 4));

		for (c = 0; c < 8; c++) {

			val16 = nvm_word((row << 3) + c, partnum);

			if (c == 4)
				printf(" ");

			printf(" %02x %02x",
			    (unsigned int)(val16 & 0xff),
			    (unsigned int)(val16 >> 8));

		}

		printf("\n");
	}
}

void
cmd_helper_swap(void)
{
	struct xstate *x = xstatus(0, NULL);
	struct xfile *f = &x->f;

	check_cmd(cmd_helper_swap, "swap");

	memcpy(
	    f->buf + (unsigned long)GBE_WORK_SIZE,
	    f->buf,
	    GBE_PART_SIZE);

	memcpy(
	    f->buf,
	    f->buf + (unsigned long)GBE_PART_SIZE,
	    GBE_PART_SIZE);

	memcpy(
	    f->buf + (unsigned long)GBE_PART_SIZE,
	    f->buf + (unsigned long)GBE_WORK_SIZE,
	    GBE_PART_SIZE);

	set_part_modified(0);
	set_part_modified(1);
}

void
cmd_helper_copy(void)
{
	struct xstate *x = xstatus(0, NULL);
	struct xfile *f = &x->f;

	check_cmd(cmd_helper_copy, "copy");

	memcpy(
	    f->buf + (unsigned long)((f->part ^ 1) * GBE_PART_SIZE),
	    f->buf + (unsigned long)(f->part * GBE_PART_SIZE),
	    GBE_PART_SIZE);

	set_part_modified(f->part ^ 1);
}

void
cmd_helper_cat(void)
{
	struct xstate *x = xstatus(0, NULL);

	check_cmd(cmd_helper_cat, "cat");

	x->cat = 0;
	cat(0);
}

void
cmd_helper_cat16(void)
{
	struct xstate *x = xstatus(0, NULL);

	check_cmd(cmd_helper_cat16, "cat16");

	x->cat = 1;
	cat(1);
}

void
cmd_helper_cat128(void)
{
	struct xstate *x = xstatus(0, NULL);

	check_cmd(cmd_helper_cat128, "cat128");

	x->cat = 15;
	cat(15);
}

void
cat(unsigned long nff)
{
	struct xstate *x = xstatus(0, NULL);
	struct xfile *f = &x->f;

	unsigned long p;
	unsigned long ff;

	p = 0;
	ff = 0;

	if ((unsigned long)x->cat != nff) {

		err(ECANCELED, "erroneous call to cat");
	}

	fflush(NULL);

	memset(f->pad, 0xff, GBE_PART_SIZE);

	for (p = 0; p < 2; p++) {

		cat_buf(f->bufcmp +
		    (unsigned long)(p * (f->gbe_file_size >> 1)));

		for (ff = 0; ff < nff; ff++) {

			cat_buf(f->pad);
		}
	}
}

void
cat_buf(unsigned char *b)
{
	if (b == NULL)
		err(errno, "null pointer in cat command");

	if (rw_file_exact(STDOUT_FILENO, b,
	    GBE_PART_SIZE, 0, IO_WRITE, LOOP_EAGAIN, LOOP_EINTR,
	    MAX_ZERO_RW_RETRY, OFF_ERR) < 0)
		err(errno, "stdout: cat");
}
void
check_cmd(void (*fn)(void),
    const char *name)
{
	struct xstate *x = xstatus(0, NULL);
	unsigned long i = x->i;

	if (x->cmd[i].run != fn)
		err(ECANCELED, "Running %s, but cmd %s is set",
		    name, x->cmd[i].str);

	/* prevent second command
	 */
	for (i = 0; i < items(x->cmd); i++)
		x->cmd[i].run = cmd_helper_err;
}

void
cmd_helper_err(void)
{
	err(ECANCELED,
	    "Erroneously running command twice");
}
