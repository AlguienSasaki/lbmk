/* SPDX-License-Identifier: MIT                                        ( >:3 )
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>                    /| |\
   Something something non-determinism                                   / \ */

#include <stdio.h>
#include <stdint.h>
#include "include/common.h"

int
main(int argc, char **argv)
{
	int same = 0;
	(void) argc, (void) argv;
	xpledgex("stdio", NULL);

	if (rsize(SIZE_MAX) == rsize(SIZE_MAX))
		same = 1;

	printf("%s\n", same ? "You win!" : "You lose!");
	return same ^ 1;
}
