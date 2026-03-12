/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2013 Free Software Foundation, Inc.
 * Copyright (c) 2023, 2026 Leah Rowe <leah@libreboot.org>
 *
 * This program receives text encoded as pulses on the PC speaker,
 * and decodes them via simple FSK (Frequency Shift Keying)
 * demodulation and FIR (Finite Impulse Response) frequency
 * discriminator.
 *
 * It waits for specific tones at specific intervals.
 * It detects tones within the audio stream and reconstructs
 * characters bit-by-bit as the encoded modem signal is received.
 * This is performance-efficient on most CPUs, and has relatively
 * high tolerance for noisy signals (similar to techniques used
 * for data stored on audio cassette tapes).
 *
 * This is a special interface provided by coreboot and GNU GRUB,
 * for computers that lack serial ports (useful for debugging).
 * Note that GRUB and coreboot can both send these signals; this
 * tool merely decodes them. This tool does not *encode*, only
 * decode.
 *
 * Usage example (NOTE: little endian!):
 * parec --channels=1 --rate=48000 --format=s16le | ./spkmodem-decode
 *
 * Originally provided by GNU GRUB, this version is a heavily
 * modified fork that complies with the OpenBSD Kernel Source
 * File Style Guide (KNF) instead of GNU coding standards; it
 * emphasises strict error handling, portability and code
 * quality, as characterised by OpenBSD projects. Several magic
 * numbers have been tidied up, calculated (not hardcoded) and
 * thoroughly explained, unlike in the original version.
 *
 * The original version was essentially a blob, masquerading as
 * source code. This forked source code is therefore the result
 * of extensive reverse engineering (of the GNU source code)!
 * This cleaned up code and extensive commenting will thoroughly
 * explain how the decoding works. This was done as an academic
 * exercise in 2023, continuing in 2026.
 *
 * This fork of spkmodem-recv, called spkmodem-decode, is provided
 * with Libreboot releases:
 * https://libreboot.org/
 *
 * The original GNU version is here, if you're morbidly curious:
 * https://cgit.git.savannah.gnu.org/cgit/grub.git/plain/util/spkmodem-recv.c?id=3dce38eb196f47bdf86ab028de74be40e13f19fd
 *
 * Libreboot's version was renamed to spkmodem-decode on 12 March 2026,
 * since Libreboot's version no longer closely resembles the GNU
 * version at all; ergo, a full rename was in order. GNU's version
 * was called spkmodem-recv.
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

/*
 * spkmodem is essentially using FSK (Frequency Shift Keying)
 * with two primary tones representing encoded bits,
 * separated by a framing tone.
 * Very cheap on CPU cycles and avoids needing something more
 * expensive like FFT or Goertzel filters, and tolerates
 * weak/noisy signals.
 */

/*
 * Frequency of audio in Hz
 */
#define SAMPLE_RATE 48000

/*
 * One analysis frame spans 5 ms.
 *
 *   frame_time = SAMPLES_PER_FRAME / SAMPLE_RATE
 *
 * With the default sample rate (48 kHz):
 *
 *   frame_time = N / 48000
 *   0.005 s = N / 48000
 *   N = 0.005 × 48000 = 240 samples
 */
#define SAMPLES_PER_FRAME 240

/*
 * Number of analysis frames per second.
 *
 * Each increment in the frequency counters corresponds
 * roughly to this many Hertz of tone frequency.
 *
 * With the default values:
 *   FRAME_RATE = 48000 / 240 = 200 Hz
 */
#define FRAME_RATE ((SAMPLE_RATE) / (SAMPLES_PER_FRAME))

/*
 * Two FIR windows are maintained; one for data tone,
 * and one for the separator tone. They are positioned
 * one frame apart in the ring buffer.
 */
#define MAX_SAMPLES (2 * (SAMPLES_PER_FRAME))

/*
 * Approx byte offset for ring buffer span, just for
 * easier debug output correlating to the audio stream.
 */
#define SAMPLE_OFFSET ((MAX_SAMPLES) * (sizeof(short)))

/*
 * Expected tone ranges (approximate, derived from spkmodem).
 * These values are intentionally wide because real-world setups
 * often involve microphones, room acoustics, and cheap ADCs.
 */
#define SEP_TONE_MIN_HZ 1000
#define SEP_TONE_MAX_HZ 3000

#define DATA_TONE_MIN_HZ 3000
#define DATA_TONE_MAX_HZ 12000

/* Mid point used to distinguish the two data tones. */
#define DATA_TONE_THRESHOLD_HZ 5000

/*
 * Convert tone frequency ranges into pulse counts within the
 * sliding analysis window.
 *
 * pulse_count is: tone_frequency / FRAME_RATE
 * where FRAME_RATE = SAMPLE_RATE / SAMPLES_PER_FRAME.
 */
#define FREQ_SEP_MIN ((SEP_TONE_MIN_HZ) / (FRAME_RATE))
#define FREQ_SEP_MAX ((SEP_TONE_MAX_HZ) / (FRAME_RATE))

#define FREQ_DATA_MIN ((DATA_TONE_MIN_HZ) / (FRAME_RATE))
#define FREQ_DATA_MAX ((DATA_TONE_MAX_HZ) / (FRAME_RATE))

#define FREQ_DATA_THRESHOLD ((DATA_TONE_THRESHOLD_HZ) / (FRAME_RATE))

/*
 * Sample amplitude threshold used to convert the waveform
 * into a pulse stream. Values near zero are regarded as noise.
 */
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

	argv0 = argv[0];

#if defined (__OpenBSD__) && defined(OpenBSD)
#if OpenBSD >= 509
	if (pledge("stdio", NULL) == -1)
		err(errno, "pledge");
#endif
#endif

	memset(&st, 0, sizeof(st));

	while ((c = getopt(argc, argv, "d")) != -1) {
		if (c != 'd')
			usage();
		st.debug = 1;
		break;
	}

	st.ascii_bit = 7;

	st.ringpos = 0;
	st.sep_pos = SAMPLES_PER_FRAME;

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

	/*
	 * If the modem signal disappears for several frames,
	 * discard the partially assembled character.
	 */
	if (st->sample_count >= (3 * SAMPLES_PER_FRAME))
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

/*
 * Verify that the observed pulse densities fall within the
 * expected ranges for spkmodem tones. This prevents random noise
 * from being misinterpreted as data.
 */
static int
valid_signal(struct decoder_state *st)
{
	return (st->freq_separator > FREQ_SEP_MIN &&
	    st->freq_separator < FREQ_SEP_MAX &&
	    st->freq_data > FREQ_DATA_MIN &&
	    st->freq_data < FREQ_DATA_MAX);
}

/*
 * Main demodulation step (moving-sum FIR filter).
 */
static void
decode_pulse(struct decoder_state *st)
{
	unsigned char old_ring, old_sep;
	unsigned char new_pulse;
	int ringpos;
	int sep_pos;
	signed short sample;

	ringpos = st->ringpos;
	sep_pos = st->sep_pos;

	/*
	 * Sliding rectangular FIR (Finite Impulse Response) filter.
	 *
	 * After thresholding, the signal becomes a stream of 0/1 pulses.
	 * The decoder measures pulse density over two windows:
	 *
	 * freq_data: pulses in the "data" window
	 * freq_separator: pulses in the "separator" window
	 *
	 * Instead of calculating each window every time (O(N) per frame), we
	 * update the window sums incrementally:
	 *
	 *   sum_new = sum_old - pulse_leaving + pulse_entering
	 *
	 * This keeps the filter O(1) per sample instead of O(N).
	 * The technique is equivalent to a rectangular FIR filter
	 * implemented as a sliding moving sum.
	 *
	 * The two windows are exactly SAMPLES_PER_FRAME apart in the ring
	 * buffer, so the pulse leaving the data window is simultaneously
	 * entering the separator window.
	 */
	old_ring = st->pulse[ringpos];
	old_sep  = st->pulse[sep_pos];
	st->freq_data -= old_ring;
	st->freq_data += old_sep;
	st->freq_separator -= old_sep;

	sample = read_sample(st);

	/*
	 * Convert the waveform sample into a pulse (0 or 1).
	 *
	 * The unsigned comparison creates a small dead zone near zero,
	 * suppressing small amplitude noise from microphones or
	 * cheap ADCs. Real PC speaker tones are far outside this
	 * range, so they still produce clean pulses.
	 */
	if ((unsigned)(sample + THRESHOLD)
	    > (unsigned)(2 * THRESHOLD))
		new_pulse = 1;
	else
		new_pulse = 0;

	st->pulse[ringpos] = new_pulse;
	st->freq_separator += new_pulse;

	/*
	 * Advance both FIR windows through the ring buffer.
	 * The separator window always stays one frame ahead
	 * of the data window.
	 */
	ringpos++;
	if (ringpos >= MAX_SAMPLES)
		ringpos = 0;
	sep_pos++;
	if (sep_pos >= MAX_SAMPLES)
		sep_pos = 0;

	st->ringpos = ringpos;
	st->sep_pos = sep_pos;

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

/*
 * Each validated frame contributes one bit of modem data.
 * Bits are accumulated MSB-first into the ASCII byte.
 */
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

	int data_hz = st->freq_data * FRAME_RATE;
	int sep_hz  = st->freq_separator * FRAME_RATE;

	if ((pos = ftell(stdin)) == -1) {
		printf("%d %d %d data=%dHz sep=%dHz\n",
		    st->freq_data,
		    st->freq_separator,
		    FREQ_DATA_THRESHOLD,
		    data_hz,
		    sep_hz);
		return;
	}

	printf("%d %d %d @%ld data=%dHz sep=%dHz\n",
	    st->freq_data,
	    st->freq_separator,
	    FREQ_DATA_THRESHOLD,
	    pos - SAMPLE_OFFSET,
	    data_hz,
	    sep_hz);
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
