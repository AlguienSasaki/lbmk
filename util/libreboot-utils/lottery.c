/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 */

#ifdef __OpenBSD__
#include <sys/param.h> /* pledge(2) */
#endif

#include <stdio.h>
#include <stdlib.h>
#include "include/common.h"

int
main(int argc, char *argv[])
{
	char *s1 = NULL;
	int rval = 0;

#if defined(__OpenBSD__) && defined(OpenBSD)
#if (OpenBSD) >= 509
	if (pledge("stdio", NULL) == -1)
		err_no_cleanup(0, errno, "openbsd won it");
#endif
#endif
	setvbuf(stdout, NULL, _IONBF, 0);

	if (win_lottery(&s1))
		rval = 1;

	if (s1 != NULL) {
		printf("%s\n\n", s1);
		free(s1);
	}

	printf("%s\n", rval ? "You won!" : "You lose! Sorry!");
	return rval? EXIT_SUCCESS : EXIT_FAILURE;
}/*

  ( >:3 )    
   /| |\
    / \                                                                      */
