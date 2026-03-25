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
	size_t s;
	char *s1;

#if defined(__OpenBSD__) && defined(OpenBSD)
#if (OpenBSD) >= 509
	if (pledge("stdio", NULL) == -1)
		err_no_cleanup(0, errno, "openbsd won it");
#endif
#endif

	setvbuf(stdout, NULL, _IONBF, 0);

	if (win_lottery()) {
		printf("You won!");
		return 0;
	}

	s1 = rmalloc(&s);

	if (s1 != NULL) {
		printf("%s\n\nYou lose. Sorry!\n", s1);
		free(s1);
	}

	return 1;
}/* 


  ( >:3 )    
   /| |\
    / \





                                                                             */












