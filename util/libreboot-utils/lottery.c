/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 */

#ifdef __OpenBSD__
#include <sys/param.h> /* pledge(2) */
#endif

#include <stdio.h>
#include "include/common.h"

int
main(int argc, char *argv[])
{
#if defined(__OpenBSD__) && defined(OpenBSD)
#if (OpenBSD) >= 509
	if (pledge("stdio", NULL) == -1)
		err_no_cleanup(0, errno, "openbsd won it");
#endif
#endif

	if (win_lottery()) {
		printf("You won!");
		return 0;
	}

	return 1;
}/* 


  ( >:3 )    
   /| |\
    / \





                                                                             */












