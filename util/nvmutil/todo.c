/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 *
 * Five Year Plan
 */

/*
 * Major TODO: split this into multiple files.
 * This program has become quite large now, mostly
 * due to all the extra sanity checks / portability.
 * Make most of nvmutil a *library* for re-use
 *
 * TODO: gettimeofday not posible - use portable functions.
 * TODO: ux fallback: modify the program instead
 * to run on 16-bit systems: smaller buffers, and do
 * operations byte-based instead of word-based.
 *
 * TODO: _XOPEN_SOURCE 500 probably not needed anymore.
 * the portable fallbacks alone are likely enough.
 * e.g. i don't need stdint, and i don't use pwrite/pread
 * anymore.
 *
 * TODO: version detection of various BSDs to detect
 * arc4random, use that if available. but also work on
 * older versions of those BSDs (also MacOS) that lack it.
 *
 * TODO: portability/testing on non-Unix systems:
 * old DOS. all windows versions (probably irrelevant
 * because you can use cygwin/wsl, whatever), classic MacOS,
 * also test really old unix e.g. sunos and irix. Be/Haiku too!
 *
 * TODO: reliance on global variables for status. make
 * functions use structs passed as args instead, make
 * functions re-useable (including libraries), etc.
 *
 * TODO: bound checks for files per-command, e.g. only
 * first 6 bytes for CMD_SETMAC
 *
 * TODO: in command sanitizer: verify that each given
 * entry corresponds to the correct function, in the
 * pointer (this check is currently missing)
 *
 * TODO: general modularisierung of the entire codebase.
 * TODO: better explain copy/swap read inversion trick
 *       by improving existing comments
 * TODO: lots of overwritten comments in code. tidy it up.
 *
 * TODO: use getopt for nvmutil args, so that multiple
 *      operations can be performed, and also on many
 *	files at once (noting limitations with cat)
 *	BONUS: implement own getopt(), for portability
 *
 * TODO: document fuzzing / analysis methods
 * 	for the code, and:
 * TODO: implement rigorous unit tests (separate util)
 *	NOTE: this would *include* known good test files
 *	in various configurations, also invalid files.
 *	the tests would likely be portable posix shell
 *	scripts rather than a new C program, but a modularisiert
 *	codebase would allow me to write a separate C
 *	program to test some finer intricacies
 * TODO: the unit tests would basically test regressions
 * TODO: after writing back a gbe to file, x_i_close() and
 *	open() it again, read it again, and check that
 *	the contents were written correctly, providing
 *	a warning if they were. do this in the main
 *	program.
 * TODO: the unit tests would include an aggressive set
 *	of fuzz tests, under controlled conditions
 *
 * TODO: also document the layout of Intel GbE files, so
 *       that wily individuals can easily expand the
 *	featureset of nvmutil.
 * TODO: write a manpage
 * TODO: simplify the command sanitization, implement more
 *	of it as build time checks, e.g. asserts.
 *	generally remove cleverness from the code, instead
 *	prefyerring readibility
 * TODO: also document nvmutil's coding style, which is
 *	its own style at this point!
 * TODO: when all the above (and possibly more) is done,
 *	submit this tool to coreboot with a further change
 *	to their build system that lets users modify
 *	GbE images, especially set MAC addresses, when
 *	including GbE files in coreboot configs.
 */
/*
 BONUS TODO:
 CI/CD. woodpecker is good enough, sourcehut also has one.
	tie this in with other things mentioned here, 
	e.g. fuzzer / unit tests
*/

/* Major TODO: reproducible builds
Test with and without these:

CFLAGS += -fno-record-gcc-switches
CFLAGS += -ffile-prefix-map=$(PWD)=.
CFLAGS += -fdebug-prefix-map=$(PWD)=.

I already avoid unique timestamps per-build,
by not using them, e.g. not reporting build
time in the program.

When splitting the nvmutil.c file later, do e.g.:

SRC = main.c io.c nvm.c cmd.c
OBJ = $(SRC:.c=.o)

^ explicitly declare the order in which to build
*/

/*
TODO:
further note when fuzzing is implemented:
use deterministic randomisation, with a
guaranteed seed - so e.g. don't use /dev/urandom
in test builds. e.g. just use normal rand()
but with a seed e.g. 1234
*/
/*
TODO: stricter build flags, e.g.
CFLAGS += -fstack-protector-strong
CFLAGS += -fno-common
CFLAGS += -D_FORTIFY_SOURCE=2
CFLAGS += -fPIE

also consider:
-fstack-clash-protection
-Wl,-z,relro
-Wl,-z,now
*/

