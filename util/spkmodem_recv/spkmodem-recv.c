/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2013 Free Software Foundation, Inc. */
/* Usage: parec --channels=1 --rate=48000 --format=s16le | ./spkmodem-recv */

/* Forked from coreboot's version, at util/spkmodem_recv/ in coreboot.git,
 * revision 5c2b5fcf2f9c9259938fd03cfa3ea06b36a007f0 as of 3 January 2022.
 * This version is heavily modified, re-written based on OpenBSD Kernel Source
 * File Style Guide (KNF); this change is Copyright 2023,2026 Leah Rowe. */

#define _POSIX_SOURCE

#ifdef __OpenBSD__
#include <sys/param.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int set_ascii_bit(void);
static void handle_audio(void), decode_pulse(void), print_char(void),
    print_stats(void);
static void err(int nvm_errval, const char *msg, ...);
static const char * getnvmprogname(void);
static void set_err_if_unset(int x);

int getopt(int, char * const *, const char *);
extern char *optarg;
extern int optind, opterr, optopt;

#if defined(__OpenBSD__) && defined(OpenBSD)
#if OpenBSD >= 509
#ifndef HAVE_PLEDGE
#define HAVE_PLEDGE 1
#endif
#endif
#endif

#define SAMPLES_PER_FRAME 240
#define MAX_SAMPLES (2 * SAMPLES_PER_FRAME)
#define FREQ_SEP_MIN 5
#define FREQ_SEP_MAX 15
#define FREQ_DATA_MIN 15
#define FREQ_DATA_THRESHOLD 25
#define FREQ_DATA_MAX 60
#define THRESHOLD 500

#define reset_char() ascii = 0, ascii_bit = 7

signed short frame[MAX_SAMPLES], pulse[MAX_SAMPLES];
int ringpos, debug, freq_data, freq_separator, sample_count, ascii_bit = 7;
char ascii = 0;

static const char *argv0;

int
main(int argc, char *argv[])
{
	int c = 0;

	argv0 = argv[0];

#ifdef HAVE_PLEDGE
#if HAVE_PLEDGE > 0
	if (pledge("stdio", NULL) == -1)
		err(errno, "pledge");
#endif
#endif
	while (c != -1) {
		c = getopt(argc, argv, "d");

		if (c == 'd')
			debug = 1;
		else if (c != -1)
			err(EINVAL, "Invalid argument");
	}

	setvbuf(stdout, NULL, _IONBF, 0);

	while (1)
		handle_audio();

	return EXIT_SUCCESS;
}

static void
handle_audio(void)
{
	int sample;

	if (sample_count > (3 * SAMPLES_PER_FRAME))
		sample_count = reset_char();

	if ((freq_separator <= FREQ_SEP_MIN) || (freq_separator >= FREQ_SEP_MAX)
	    || (freq_data <= FREQ_DATA_MIN) || (freq_data >= FREQ_DATA_MAX)) {
		decode_pulse();
		return;
	}

	if (set_ascii_bit() < 0)
		print_char();

	sample_count = 0;

	for (sample = 0; sample < SAMPLES_PER_FRAME; sample++)
		decode_pulse();
}

static void
decode_pulse(void)
{
	size_t n;
	int next_ringpos;

	next_ringpos = (ringpos + SAMPLES_PER_FRAME) % MAX_SAMPLES;

	freq_data -= pulse[ringpos];
	freq_data += pulse[next_ringpos];
	freq_separator -= pulse[next_ringpos];

	n = fread(&frame[ringpos], 1, sizeof(frame[0]), stdin);
	if (n != sizeof(frame[0])) {
		if (feof(stdin))
			exit(EXIT_SUCCESS);
		err(errno, "stdin read");
	}

	pulse[ringpos] = (abs(frame[ringpos]) > THRESHOLD) ? 1 : 0;

	freq_separator += pulse[ringpos];
	ringpos = (ringpos + 1) % MAX_SAMPLES;

	++sample_count;
}

static int
set_ascii_bit(void)
{
	if (debug)
		print_stats();

	if (freq_data < FREQ_DATA_THRESHOLD)
		ascii |= (1 << ascii_bit);

	--ascii_bit;
	return ascii_bit;
}

static void
print_stats(void)
{
	long stdin_pos;

	if ((stdin_pos = ftell(stdin)) == -1)
		err(errno, "ftell stdin");

	printf ("%d %d %d @%ld\n", freq_data, freq_separator,
	    FREQ_DATA_THRESHOLD, stdin_pos - sizeof(frame));
}

static void
print_char(void)
{
	if (debug)
		printf("<%c, %x>", ascii, ascii);
	else
		printf("%c", ascii);

	reset_char();
}

static void
err(int nvm_errval, const char *msg, ...)
{
	va_list args;

	fprintf(stderr, "%s: ", getnvmprogname());

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);

	set_err_if_unset(nvm_errval);
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
set_err_if_unset(int x)
{
	if (errno)
		return;
	if (x > 0)
		errno = x;
	else
		errno = ECANCELED;
}
