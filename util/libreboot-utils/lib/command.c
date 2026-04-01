/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022-2026 Leah Rowe <leah@libreboot.org>
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

void
sanitize_command_list(void)
{
	struct xstate *x = xstatus();

	size_t c;
	size_t num_commands;

	num_commands = items(x->cmd);

	for (c = 0; c < num_commands; c++)
		sanitize_command_index(c);
}

void
sanitize_command_index(size_t c)
{
	struct xstate *x = xstatus();
	struct commands *cmd = &x->cmd[c];

	int _flag;
	size_t gbe_rw_size;

	size_t rval;

	check_command_num(c);

	if (cmd->argc < 3)
		exitf("cmd index %lu: argc below 3, %d",
		    (size_t)c, cmd->argc);

	if (cmd->str == NULL)
		exitf("cmd index %lu: NULL str",
		    (size_t)c);

	if (*cmd->str == '\0')
		exitf("cmd index %lu: empty str",
		    (size_t)c);

	if (slen(cmd->str, MAX_CMD_LEN +1, &rval) > MAX_CMD_LEN) {
		exitf("cmd index %lu: str too long: %s",
		    (size_t)c, cmd->str);
	}

	if (cmd->run == NULL)
		exitf("cmd index %lu: cmd ptr null",
		    (size_t)c);

	check_bin(cmd->arg_part, "cmd.arg_part");
	check_bin(cmd->chksum_read, "cmd.chksum_read");
	check_bin(cmd->chksum_write, "cmd.chksum_write");

	gbe_rw_size = cmd->rw_size;

	switch (gbe_rw_size) {
	case GBE_PART_SIZE:
	case NVM_SIZE:
		break;
	default:
		exitf("Unsupported rw_size: %lu",
		    (size_t)gbe_rw_size);
	}

	if (gbe_rw_size > GBE_PART_SIZE)
		exitf("rw_size larger than GbE part: %lu",
		    (size_t)gbe_rw_size);

	_flag = (cmd->flags & O_ACCMODE);

	if (_flag != O_RDONLY &&
	    _flag != O_RDWR)
		exitf("invalid cmd.flags setting");
}

void
set_cmd(int argc, char *argv[])
{
	struct xstate *x = xstatus();
	const char *cmd;

	int rval;

	size_t c;

	for (c = 0; c < items(x->cmd); c++) {

		cmd = x->cmd[c].str;

		if (scmp(argv[2], cmd, MAX_CMD_LEN, &rval))
			continue; /* not the right command */

		/* valid command found */
		if (argc >= x->cmd[c].argc) {
			x->no_cmd = 0;
			x->i = c; /* set command */

			return;
		}

		exitf(
		    "Too few args on command '%s'", cmd);
	}


	x->no_cmd = 1;
}

void
set_cmd_args(int argc, char *argv[])
{
	struct xstate *x = xstatus();
	size_t i = x->i;
	struct commands *cmd = &x->cmd[i];
	struct xfile *f = &x->f;

	if (!valid_command(i) || argc < 3)
		usage();

	if (x->no_cmd)
		usage();

	/* Maintainer bug
	 */
	if (cmd->arg_part && argc < 4)
		exitf(
		    "arg_part set for command that needs argc4");

	if (cmd->arg_part && i == CMD_SETMAC)
		exitf(
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

size_t
conv_argv_part_num(const char *part_str)
{
	unsigned char ch;

	if (part_str[0] == '\0' || part_str[1] != '\0')
		exitf("Partnum string '%s' wrong length", part_str);

	/* char signedness is implementation-defined
	 */
	ch = (unsigned char)part_str[0];
	if (ch < '0' || ch > '1')
		exitf("Bad part number (%c)", ch);

	return (size_t)(ch - '0');
}

void
check_command_num(size_t c)
{
	if (!valid_command(c))
		exitf("Invalid run_cmd arg: %lu",
		    (size_t)c);
}

unsigned char
valid_command(size_t c)
{
	struct xstate *x = xstatus();
	struct commands *cmd;

	if (c >= items(x->cmd))
		return 0;

	cmd = &x->cmd[c];

	if (c != cmd->chk)
		exitf(
		    "Invalid cmd chk value (%lu) vs arg: %lu",
		    cmd->chk, c);

	return 1;
}

void
cmd_helper_setmac(void)
{
	struct xstate *x = xstatus();
	struct macaddr *mac = &x->mac;

	size_t partnum;

	check_cmd(cmd_helper_setmac, "setmac");

	printf("MAC address to be written: %s\n", mac->str);
	parse_mac_string();

	for (partnum = 0; partnum < 2; partnum++)
		write_mac_part(partnum);
}

void
parse_mac_string(void)
{
	struct xstate *x = xstatus();
	struct macaddr *mac = &x->mac;

	size_t mac_byte;

	size_t rval;

	if (slen(x->mac.str, 18, &rval) != 17)
		exitf("MAC address is the wrong length");

	memset(mac->mac_buf, 0, sizeof(mac->mac_buf));

	for (mac_byte = 0; mac_byte < 6; mac_byte++)
		set_mac_byte(mac_byte);

	if ((mac->mac_buf[0] | mac->mac_buf[1] | mac->mac_buf[2]) == 0)
		exitf("Must not specify all-zeroes MAC address");

	if (mac->mac_buf[0] & 1)
		exitf("Must not specify multicast MAC address");
}

void
set_mac_byte(size_t mac_byte_pos)
{
	struct xstate *x = xstatus();
	struct macaddr *mac = &x->mac;

	char separator;

	size_t mac_str_pos;
	size_t mac_nib_pos;

	mac_str_pos = mac_byte_pos * 3;

	if (mac_str_pos < 15) {
		if ((separator = mac->str[mac_str_pos + 2]) != ':')
			exitf("Invalid MAC address separator '%c'",
			    separator);
	}

	for (mac_nib_pos = 0; mac_nib_pos < 2; mac_nib_pos++)
		set_mac_nib(mac_str_pos, mac_byte_pos, mac_nib_pos);
}

void
set_mac_nib(size_t mac_str_pos,
    size_t mac_byte_pos, size_t mac_nib_pos)
{
	struct xstate *x = xstatus();
	struct macaddr *mac = &x->mac;

	char mac_ch;
	unsigned short hex_num;

	mac_ch = mac->str[mac_str_pos + mac_nib_pos];

	if ((hex_num = hextonum(mac_ch)) > 15) {
		if (hex_num >= 17)
			exitf("Randomisation failure");
		else
			exitf("Invalid character '%c'",
			    mac->str[mac_str_pos + mac_nib_pos]);
	}

	/* If random, ensure that local/unicast bits are set.
	 */
	if ((mac_byte_pos == 0) && (mac_nib_pos == 1) &&
	    ((mac_ch | 0x20) == 'x' ||
	    (mac_ch == '?')))
		hex_num = (hex_num & 0xE) | 2; /* local, unicast */

	/* MAC words stored big endian in-file, little-endian
	 * logically, so we reverse the order.
	 */
	mac->mac_buf[mac_byte_pos >> 1] |= hex_num <<
	    (((mac_byte_pos & 1) << 3) /* left or right byte? */
	    | ((mac_nib_pos ^ 1) << 2)); /* left or right nib? */
}

void
write_mac_part(size_t partnum)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;
	struct macaddr *mac = &x->mac;

	size_t w;

	check_bin(partnum, "part number");
	if (!f->part_valid[partnum])
		return;

	for (w = 0; w < 3; w++)
		set_nvm_word(w, partnum, mac->mac_buf[w]);

	printf("Wrote MAC address to part %lu: ",
	    (size_t)partnum);
	print_mac_from_nvm(partnum);
}

void
cmd_helper_dump(void)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	size_t p;

	check_cmd(cmd_helper_dump, "dump");

	f->part_valid[0] = good_checksum(0);
	f->part_valid[1] = good_checksum(1);

	for (p = 0; p < 2; p++) {

		if (!f->part_valid[p]) {

			fprintf(stderr,
			    "BAD checksum %04x in part %lu (expected %04x)\n",
			    nvm_word(NVM_CHECKSUM_WORD, p),
			    (size_t)p,
			    calculated_checksum(p));
		}

		printf("MAC (part %lu): ",
		    (size_t)p);

		print_mac_from_nvm(p);
		spew_hex(f->buf + (p * GBE_PART_SIZE), NVM_SIZE);
	}
}

void
print_mac_from_nvm(size_t partnum)
{
	size_t c;
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
cmd_helper_swap(void)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	check_cmd(cmd_helper_swap, "swap");

	memcpy(
	    f->buf + (size_t)GBE_WORK_SIZE,
	    f->buf,
	    GBE_PART_SIZE);

	memcpy(
	    f->buf,
	    f->buf + (size_t)GBE_PART_SIZE,
	    GBE_PART_SIZE);

	memcpy(
	    f->buf + (size_t)GBE_PART_SIZE,
	    f->buf + (size_t)GBE_WORK_SIZE,
	    GBE_PART_SIZE);

	set_part_modified(0);
	set_part_modified(1);
}

void
cmd_helper_copy(void)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	check_cmd(cmd_helper_copy, "copy");

	memcpy(
	    f->buf + (size_t)((f->part ^ 1) * GBE_PART_SIZE),
	    f->buf + (size_t)(f->part * GBE_PART_SIZE),
	    GBE_PART_SIZE);

	set_part_modified(f->part ^ 1);
}

void
cmd_helper_cat(void)
{
	struct xstate *x = xstatus();

	check_cmd(cmd_helper_cat, "cat");

	x->cat = 0;
	cat(0);
}

void
cmd_helper_cat16(void)
{
	struct xstate *x = xstatus();

	check_cmd(cmd_helper_cat16, "cat16");

	x->cat = 1;
	cat(1);
}

void
cmd_helper_cat128(void)
{
	struct xstate *x = xstatus();

	check_cmd(cmd_helper_cat128, "cat128");

	x->cat = 15;
	cat(15);
}

void
cat(size_t nff)
{
	struct xstate *x = xstatus();
	struct xfile *f = &x->f;

	size_t p;
	size_t ff;

	p = 0;
	ff = 0;

	if ((size_t)x->cat != nff) {

		exitf("erroneous call to cat");
	}

	fflush(NULL);

	memset(f->pad, 0xff, GBE_PART_SIZE);

	for (p = 0; p < 2; p++) {

		cat_buf(f->bufcmp +
		    (size_t)(p * (f->gbe_file_size >> 1)));

		for (ff = 0; ff < nff; ff++) {

			cat_buf(f->pad);
		}
	}
}

void
cat_buf(unsigned char *b)
{
	if (b == NULL)
		exitf("null pointer in cat command");

	if (rw_exact(STDOUT_FILENO, b,
	    GBE_PART_SIZE, 0, IO_WRITE) < 0)
		exitf("stdout: cat");
}
void
check_cmd(void (*fn)(void),
    const char *name)
{
	struct xstate *x = xstatus();
	size_t i = x->i;

	if (x->cmd[i].run != fn)
		exitf("Running %s, but cmd %s is set",
		    name, x->cmd[i].str);

	/* prevent second command
	 */
	for (i = 0; i < items(x->cmd); i++)
		x->cmd[i].run = cmd_helper_err;
}

void
cmd_helper_err(void)
{
	exitf(
	    "Erroneously running command twice");
}
