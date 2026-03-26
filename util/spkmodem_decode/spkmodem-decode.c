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
#include <limits.h>
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
 * WARNING: if changing, make sure to adjust
 *     SAMPLES_PER_FRAME accordingly (see maths below)
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

#define SEP_TOLERANCE_PULSES \
    (((SEP_TONE_MAX_HZ) - (SEP_TONE_MIN_HZ)) / (2 * (FRAME_RATE)))

#define DATA_TONE_MIN_HZ 3000
#define DATA_TONE_MAX_HZ 12000

/* Mid point used to distinguish the two data tones. */
#define DATA_TONE_THRESHOLD_HZ 5000

/*
 * Convert tone frequency ranges into pulse counts within the
 * sliding analysis window.
 *
 * pulse_count = tone_frequency / FRAME_RATE
 * where FRAME_RATE = SAMPLE_RATE / SAMPLES_PER_FRAME.
 */
#define FREQ_SEP_MIN ((SEP_TONE_MIN_HZ) / (FRAME_RATE))
#define FREQ_SEP_MAX ((SEP_TONE_MAX_HZ) / (FRAME_RATE))

#define FREQ_DATA_MIN ((DATA_TONE_MIN_HZ) / (FRAME_RATE))
#define FREQ_DATA_MAX ((DATA_TONE_MAX_HZ) / (FRAME_RATE))

#define FREQ_DATA_THRESHOLD ((DATA_TONE_THRESHOLD_HZ) / (FRAME_RATE))

/*
 * These determine how long the program will wait during
 * tone auto-detection, before shifting to defaults.
 * It is done every LEARN_FRAMES number of frames.
 */
#define LEARN_SECONDS 1
#define LEARN_FRAMES ((LEARN_SECONDS) * (FRAME_RATE))

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

	/* dynamic separator calibration */
	int sep_sum;
	int sep_samples;
	int sep_min;
	int sep_max;

	/* for automatic tone detection */
	int freq_min;
	int freq_max;
	int freq_threshold;
	int learn_frames;

	/* previous sample used for edge detection */
	signed short prev_sample;
};

static const char *argv0;

/*
 * 16-bit little endian words are read
 * continuously. we will swap them, if
 * the host cpu is big endian.
 */
static int host_is_big_endian(void);

/* main loop */
static void handle_audio(struct decoder_state *st);

/* separate tone tolerances */
static void select_separator_tone(struct decoder_state *st);
static int is_valid_signal(struct decoder_state *st);

/* output to terminal */
static int set_ascii_bit(struct decoder_state *st);
static void print_char(struct decoder_state *st);
static void reset_char(struct decoder_state *st);

/* process samples/frames */
static void decode_pulse(struct decoder_state *st);
static signed short read_sample(struct decoder_state *st);
static void read_words(struct decoder_state *st);

/* continually adjust tone */
static void detect_tone(struct decoder_state *st);
static int silent_signal(struct decoder_state *st);
static void select_low_tone(struct decoder_state *st);

/* debug */
static void print_stats(struct decoder_state *st);

/* error handling / usage */
static void err(int errval, const char *msg, ...);
static void usage(void);
static const char *progname(void);

/* portability (old systems) */
int getopt(int, char * const *, const char *);
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

typedef char static_assert_char_is_8_bits[(CHAR_BIT == 8) ? 1 : -1];
typedef char static_assert_char_is_1[(sizeof(char) == 1) ? 1 : -1];
typedef char static_assert_short[(sizeof(short) == 2) ? 1 : -1];
typedef char static_assert_int_is_4[(sizeof(int) >= 4) ? 1 : -1];
typedef char static_assert_twos_complement[
    ((-1 & 3) == 3) ? 1 : -1
];

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

	/* fallback in case tone detection fails */
	st.freq_min = 100000;
	st.freq_max = 0;
	st.freq_threshold = FREQ_DATA_THRESHOLD;

	/*
	 * Used for separator calibration
	 */
	st.sep_min = FREQ_SEP_MIN;
	st.sep_max = FREQ_SEP_MAX;

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
	 * If the modem signal disappears for several (read: 3)
	 * frames, discard the partially assembled character.
	 */
	if (st->sample_count >= (3 * SAMPLES_PER_FRAME) ||
	    st->freq_separator <= 0)
		reset_char(st);

	st->sample_count = 0;

	/* process exactly one frame */
	for (sample = 0; sample < SAMPLES_PER_FRAME; sample++)
		decode_pulse(st);

	select_separator_tone(st);

	if (set_ascii_bit(st) < 0)
		print_char(st);

	/* Detect tone per each frame */
	detect_tone(st);
}

/*
 * collect separator tone statistics
 * (and auto-adjust tolerances)
 */
static void
select_separator_tone(struct decoder_state *st)
{
	int avg;

	if (!is_valid_signal(st))
		return;

	st->sep_sum += st->freq_separator;
	st->sep_samples++;

	if (st->sep_samples != 50)
		return;

	avg = st->sep_sum / st->sep_samples;

	st->sep_min = avg - SEP_TOLERANCE_PULSES;
	st->sep_max = avg + SEP_TOLERANCE_PULSES;

	/* reset calibration accumulators */
	st->sep_sum = 0;
	st->sep_samples = 0;

	if (st->debug)
		printf("separator calibrated: %dHz\n",
		    avg * FRAME_RATE);
}

/*
 * Verify that the observed pulse densities fall within the
 * expected ranges for spkmodem tones. This prevents random noise
 * from being misinterpreted as data.
 */
static int
is_valid_signal(struct decoder_state *st)
{
	if (st->freq_data <= 0)
		return 0;

	if (st->freq_separator < st->sep_min ||
	    st->freq_separator > st->sep_max)
		return 0;

	return 1;
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

	if (!is_valid_signal(st))
		return st->ascii_bit;

	if (st->freq_data < st->freq_threshold)
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
reset_char(struct decoder_state *st)
{
	st->ascii = 0;
	st->ascii_bit = 7;
}

/*
 * Main demodulation step (moving-sum FIR filter).
 */
static void
decode_pulse(struct decoder_state *st)
{
	unsigned char old_ring, old_sep;
	unsigned char new_pulse;
	signed short sample;
	int ringpos;
	int sep_pos;
	int diff_edge;
	int diff_amp;

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
	 * Avoid startup edge. Since
	 * it's zero at startup, this
	 * may wrongly produce a pulse
	 */
	if (st->sample_count == 0)
		st->prev_sample = sample;

	/*
	 * Detect edges instead of amplitude.
	 * This is more tolerant of weak microphones
	 * and speaker distortion..
	 *
	 * However, we check both slope edges and
	 * amplitude, to mitagate noise.
	 */
	diff_amp = sample;
	diff_edge = sample - st->prev_sample;
	if (diff_edge < 0)
		diff_edge = -diff_edge;
	if (diff_amp < 0)
		diff_amp = -diff_amp;
	if (diff_edge > THRESHOLD &&
	    diff_amp > THRESHOLD)
		new_pulse = 1;
	else
		new_pulse = 0;
	st->prev_sample = sample;

	st->pulse[ringpos] = new_pulse;
	st->freq_separator += new_pulse;

	/*
	 * Advance both FIR windows through the ring buffer.
	 * The separator window always stays one frame ahead
	 * of the data window.
	 */
	if (++ringpos >= MAX_SAMPLES)
		ringpos = 0;
	if (++sep_pos >= MAX_SAMPLES)
		sep_pos = 0;

	st->ringpos = ringpos;
	st->sep_pos = sep_pos;

	st->sample_count++;
}

static signed short
read_sample(struct decoder_state *st)
{
	signed short sample;
	unsigned short u;

	while (st->inpos >= st->inlen)
		read_words(st);

	sample = st->inbuf[st->inpos++];

	if (st->swap_bytes) {
		u = (unsigned short)sample;
		u = (u >> 8) | (u << 8);

		sample = (signed short)u;
	}

	return sample;
}

static void
read_words(struct decoder_state *st)
{
	size_t n;

	n = fread(st->inbuf, sizeof(st->inbuf[0]),
	    READ_BUF, stdin);

	if (n != 0) {
		st->inpos = 0;
		st->inlen = n;

		return;
	}

	if (ferror(stdin))
		err(errno, "stdin read");
	if (feof(stdin))
		exit(EXIT_SUCCESS);
}

/*
 * Automatically detect spkmodem tone
 */
static void
detect_tone(struct decoder_state *st)
{
	if (st->learn_frames >= LEARN_FRAMES)
		return;

	st->learn_frames++;

	if (silent_signal(st))
		return;

	select_low_tone(st);

	if (st->learn_frames != LEARN_FRAMES)
		return;

	/*
	 * If the observed frequencies are too close,
	 * learning likely failed (only one tone seen).
	 * Keep the default threshold.
	 */
	if (st->freq_max - st->freq_min < 2)
		return;

	st->freq_threshold =
	    (st->freq_min + st->freq_max) / 2;

	if (st->debug)
		printf("auto threshold: %dHz\n",
		    st->freq_threshold * FRAME_RATE);
}

/*
 * Ignore silence / near silence.
 * Both FIR windows will be near zero when no signal exists.
 */
static int
silent_signal(struct decoder_state *st)
{
	return (st->freq_data <= 2 &&
	    st->freq_separator <= 2);
}

/*
 * Choose the lowest active tone.
 * Separator frames carry tone in the separator window,
 * data frames carry tone in the data window.
 */
static void
select_low_tone(struct decoder_state *st)
{
	int f;

	f = st->freq_data;

	if (f <= 0 || (st->freq_separator > 0 &&
	    st->freq_separator < f))
		f = st->freq_separator;

	if (f <= 0)
		return;

	if (f < st->freq_min)
		st->freq_min = f;

	if (f > st->freq_max)
		st->freq_max = f;
}

static void
print_stats(struct decoder_state *st)
{
	long pos;

	int data_hz = st->freq_data * FRAME_RATE;
	int sep_hz  = st->freq_separator * FRAME_RATE;
	int sep_hz_min = st->sep_min * FRAME_RATE;
	int sep_hz_max = st->sep_max * FRAME_RATE;

	if ((pos = ftell(stdin)) == -1) {
		printf("%d %d %d data=%dHz sep=%dHz(min %dHz %dHz)\n",
		    st->freq_data,
		    st->freq_separator,
		    st->freq_threshold,
		    data_hz,
		    sep_hz,
		    sep_hz_min,
		    sep_hz_max);
		return;
	}

	printf("%d %d %d @%ld data=%dHz sep=%dHz(min %dHz %dHz)\n",
	    st->freq_data,
	    st->freq_separator,
	    st->freq_threshold,
	    pos - SAMPLE_OFFSET,
	    data_hz,
	    sep_hz,
	    sep_hz_min,
	    sep_hz_max);
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
