/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2013 Free Software Foundation, Inc.
 * Copyright (c) 2023, 2026 Leah Rowe <leah@libreboot.org>
 *
 * This program receives text encoded as pulses on the PC speaker,
 * and decodes them. This is a special type of interface provided
 * by coreboot and GRUB, for computers that lack serial ports.
 *
 * Usage example (NOTE: little endian!):
 * parec --channels=1 --rate=48000 --format=s16le | ./spkmodem-recv
 *
 * Originally provided by GNU GRUB, this version is a heavily
 * modified fork that complies with the OpenBSD Kernel Source
 * File Style Guide (KNF) instead of GNU coding standards; it
 * emphasises strict error handling, portability and code
 * quality, as characterised by OpenBSD projects.
 *
 * This fork of spkmodem-recv is provided with Libreboot releases:
 * https://libreboot.org/
 */

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
#define SAMPLE_OFFSET (MAX_SAMPLES * sizeof(short))

#define FREQ_SEP_MIN 5
#define FREQ_SEP_MAX 15

#define FREQ_DATA_MIN 15
#define FREQ_DATA_THRESHOLD 25
#define FREQ_DATA_MAX 60

#define THRESHOLD 500

#define READ_BUF 4096

struct decoder_state {
	unsigned char pulse[MAX_SAMPLES];

	signed short inbuf[READ_BUF];
	size_t inpos;
	size_t inlen;

	int ringpos;
	int sep_pos;

	/*
	 * Sliding window pulse counters
	 * used to detect modem tones
	 */
	int freq_data;
	int freq_separator;
	int sample_count;

	int ascii_bit;
	unsigned char ascii;

	int debug;
	int swap_bytes;
};

static const char *argv0;

static int host_is_big_endian(void);
static void handle_audio(struct decoder_state *st);
static int valid_signal(struct decoder_state *st);
static void decode_pulse(struct decoder_state *st);
static signed short read_sample(struct decoder_state *st);
static int set_ascii_bit(struct decoder_state *st);
static void print_char(struct decoder_state *st);
static void print_stats(struct decoder_state *st);
static void reset_char(struct decoder_state *st);

static void err(int errval, const char *msg, ...);
static void usage(void);
static const char *progname(void);

int getopt(int, char * const *, const char *);
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

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

	while ((c = getopt(argc, argv, "d")) != -1) {
		if (c != 'd')
			usage();
		st.debug = 1;
		break;
	}

	if (host_is_big_endian())
		st.swap_bytes = 1;

	setvbuf(stdout, NULL, _IONBF, 0);

	for (;;)
		handle_audio(&st);

	return EXIT_SUCCESS;
}

static int
host_is_big_endian(void)
{
	unsigned int x = 1;
	return (*(unsigned char *)&x == 0);
}

static void
handle_audio(struct decoder_state *st)
{
	int sample;

	if (st->sample_count > (3 * SAMPLES_PER_FRAME))
		reset_char(st);
	if (!valid_signal(st)) {
		decode_pulse(st);
		return;
	}

	if (set_ascii_bit(st) < 0)
		print_char(st);

	st->sample_count = 0;
	for (sample = 0; sample < SAMPLES_PER_FRAME; sample++)
		decode_pulse(st);
}

static int
valid_signal(struct decoder_state *st)
{
	return (st->freq_separator > FREQ_SEP_MIN &&
	    st->freq_separator < FREQ_SEP_MAX &&
	    st->freq_data > FREQ_DATA_MIN &&
	    st->freq_data < FREQ_DATA_MAX);
}

static void
decode_pulse(struct decoder_state *st)
{
	unsigned char old_ring, old_sep;
	unsigned char new_pulse;
	int ringpos;
	signed short sample;

	ringpos = st->ringpos;

	old_ring = st->pulse[ringpos];
	old_sep = st->pulse[st->sep_pos];

	st->freq_data -= old_ring;
	st->freq_data += old_sep;
	st->freq_separator -= old_sep;

	sample = read_sample(st);

	if ((unsigned)(sample + THRESHOLD)
	    > (unsigned)(2 * THRESHOLD))
		new_pulse = 1;
	else
		new_pulse = 0;

	st->pulse[ringpos] = new_pulse;
	st->freq_separator += new_pulse;

	ringpos++;
	if (ringpos >= MAX_SAMPLES)
		ringpos = 0;

	st->ringpos = ringpos;

	st->sep_pos++;
	if (st->sep_pos >= MAX_SAMPLES)
		st->sep_pos = 0;

	st->sample_count++;
}

static signed short
read_sample(struct decoder_state *st)
{
	size_t n;
	signed short sample;
	unsigned short u;

	while (st->inpos >= st->inlen) {

		n = fread(st->inbuf, sizeof(st->inbuf[0]),
		    READ_BUF, stdin);

		if (n == 0) {
			if (ferror(stdin))
				err(errno, "stdin read");
			if (feof(stdin))
				exit(EXIT_SUCCESS);
		}

		st->inpos = 0;
		st->inlen = n;
	}

	sample = st->inbuf[st->inpos++];

	if (st->swap_bytes) {
		u = (unsigned short)sample;
		u = (u >> 8) | (u << 8);

		sample = (signed short)u;
	}

	return sample;
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

	if ((pos = ftell(stdin)) == -1) {
		printf("%d %d %d\n",
		    st->freq_data,
		    st->freq_separator,
		    FREQ_DATA_THRESHOLD);
		return;
	}

	printf("%d %d %d @%ld\n",
	    st->freq_data,
	    st->freq_separator,
	    FREQ_DATA_THRESHOLD,
	    pos - SAMPLE_OFFSET);
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

	fprintf(stderr, ": %s\n", strerror(errval));
	exit(EXIT_FAILURE);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-d]\n", progname());
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
