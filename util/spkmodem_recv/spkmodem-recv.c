/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2013 Free Software Foundation, Inc. */
/* Usage: parec --channels=1 --rate=48000 --format=s16le | ./spkmodem-recv */

/* Forked from coreboot's version, at util/spkmodem_recv/ in coreboot.git,
 * revision 5c2b5fcf2f9c9259938fd03cfa3ea06b36a007f0 as of 3 January 2022.
 * This version is heavily modified, re-written based on OpenBSD Kernel Source
 * File Style Guide (KNF); this change is Copyright 2023,2026 Leah Rowe. */

#define _POSIX_SOURCE

/*
 * For OpenBSD define, to detect version
 * for deciding whether to use pledge(2)
 */
#ifdef __OpenBSD__
#include <sys/param.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SAMPLES_PER_FRAME 240
#define MAX_SAMPLES (2 * SAMPLES_PER_FRAME)

#define FREQ_SEP_MIN 5
#define FREQ_SEP_MAX 15

#define FREQ_DATA_MIN 15
#define FREQ_DATA_THRESHOLD 25
#define FREQ_DATA_MAX 60

#define THRESHOLD 500

struct decoder_state {
	signed short frame[MAX_SAMPLES];
	unsigned char pulse[MAX_SAMPLES];

	int ringpos;
	int sep_pos;

	int freq_data;
	int freq_separator;
	int sample_count;

	int ascii_bit;
	unsigned char ascii;

	int debug;
};

static const char *argv0;

static void handle_audio(struct decoder_state *st);
static void decode_pulse(struct decoder_state *st);
static int set_ascii_bit(struct decoder_state *st);
static void print_char(struct decoder_state *st);
static void print_stats(struct decoder_state *st);
static void reset_char(struct decoder_state *st);

static void err(int errval, const char *msg, ...);
static const char *progname(void);

int getopt(int, char * const *, const char *);
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

#ifndef errno
extern int errno;
#endif

int
main(int argc, char **argv)
{
	struct decoder_state st;
	int c;

#if defined (__OpenBSD__) && defined(OpenBSD)
#if OpenBSD >= 509
	if (pledge("stdio", NULL) == -1)
		err(errno, "pledge");
#endif
#endif

	memset(&st, 0, sizeof(st));
	st.ascii_bit = 7;

	st.ringpos = 0;
	st.sep_pos = SAMPLES_PER_FRAME;

	argv0 = argv[0];

	while (1) {
		c = getopt(argc, argv, "d");
		if (c == -1)
			break;

		if (c == 'd')
			st.debug = 1;
		else
			err(EINVAL, "Invalid argument");
	}

	setvbuf(stdout, NULL, _IONBF, 0);

	for (;;)
		handle_audio(&st);

	return EXIT_SUCCESS;
}

static void
handle_audio(struct decoder_state *st)
{
	int sample;

	if (st->sample_count > (3 * SAMPLES_PER_FRAME))
		reset_char(st);

	if ((st->freq_separator <= FREQ_SEP_MIN) ||
	    (st->freq_separator >= FREQ_SEP_MAX) ||
	    (st->freq_data <= FREQ_DATA_MIN) ||
	    (st->freq_data >= FREQ_DATA_MAX)) {

		decode_pulse(st);
		return;
	}

	if (set_ascii_bit(st) < 0)
		print_char(st);

	st->sample_count = 0;

	for (sample = 0; sample < SAMPLES_PER_FRAME; sample++)
		decode_pulse(st);
}

static void
decode_pulse(struct decoder_state *st)
{
	size_t n;

	st->freq_data -= st->pulse[st->ringpos];
	st->freq_data += st->pulse[st->sep_pos];
	st->freq_separator -= st->pulse[st->sep_pos];

	n = fread(&st->frame[st->ringpos], sizeof(st->frame[0]), 1, stdin);
	if (n != 1) {
		if (feof(stdin))
			exit(EXIT_SUCCESS);
		err(errno, "stdin read");
	}

	st->pulse[st->ringpos] =
	    abs(st->frame[st->ringpos]) > THRESHOLD;

	st->freq_separator += st->pulse[st->ringpos];

	st->ringpos++;
	if (st->ringpos >= MAX_SAMPLES)
		st->ringpos = 0;

	st->sep_pos++;
	if (st->sep_pos >= MAX_SAMPLES)
		st->sep_pos = 0;

	st->sample_count++;
}

static int
set_ascii_bit(struct decoder_state *st)
{
	if (st->debug)
		print_stats(st);

	if (st->freq_data < FREQ_DATA_THRESHOLD)
		st->ascii |= (1 << st->ascii_bit);

	st->ascii_bit--;

	return st->ascii_bit;
}

static void
print_char(struct decoder_state *st)
{
	if (st->debug)
		printf("<%c,%x>", st->ascii, st->ascii);
	else
		putchar(st->ascii);

	reset_char(st);
}

static void
print_stats(struct decoder_state *st)
{
	long pos;

	pos = ftell(stdin);
	if (pos == -1)
		err(errno, "ftell");

	printf("%d %d %d @%ld\n",
	    st->freq_data,
	    st->freq_separator,
	    FREQ_DATA_THRESHOLD,
	    pos - sizeof(st->frame));
}

static void
reset_char(struct decoder_state *st)
{
	st->ascii = 0;
	st->ascii_bit = 7;
}

static void
err(int errval, const char *msg, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", progname());

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);

	if (!errno)
		errno = errval;

	fprintf(stderr, ": %s\n", strerror(errno));
	exit(EXIT_FAILURE);
}

static const char *
progname(void)
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
