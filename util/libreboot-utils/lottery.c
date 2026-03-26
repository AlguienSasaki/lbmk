/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>
 */
#include <stdio.h>
#include <string.h>
#include "include/common.h"
static int rigged(char **s);

int
main(int argc, char *argv[])
{
#ifdef __OpenBSD__
	if (pledge("stdio", NULL) == -1)
		err_no_cleanup(0, errno, "openbsd wins");
#endif
	printf("%s\n", (argc = rigged(argv)) ? "You lose!" : "You win!");
	return argc;
}

static int
rigged(char **s) /* are u lucky? */
{
	size_t size[2] = { rsize(1 << 17), rsize(1 << 17) };

	return !(size[0] && size[0] == size[1] && size[0] <= 1 << 18 &&
	    s != NULL) || memcmp(*s = mkrbuf(size[0] << 1), *s + size[0],
	    size[0]);
}/* ( >:3 )    
     /| |\      it could be you!
      / \                                                                     */