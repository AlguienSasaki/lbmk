/* SPDX-License-Identifier: MIT                                         ( >:3 )
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>                     /| |\
                                                                          / \*/
#include <stdio.h>
#include <string.h> /*                           (^.>)  - are u lucky?       */
#include "include/common.h" /*                   \| /=                       */
static int rigged(char **s, int spew) { /*        l \                        */
	size_t size[2] = { rsize(1 << 17), rsize(1 << 17) };
	spew &= size[1] = !(size[0] && size[0] == size[1] && size[0] <=1<<18 &&
	    s != NULL) || memcmp(*s = mkrbuf(size[0] << 1), *s + size[0],
	    size[0]);
	if (spew && !size[1]) printf("%s\n\n", *s);
	return size[1];
} /*                                             (^:3) - it could be you!    */
int main(int argc, char **argv) { /*             /| |l                       */
	xpledgex("stdio", NULL); /*               / \                        */
	printf("%s\n", (argc = rigged(argv, argc > 1))?"You lose!":"You win!");
	return argc; /*                          (^.^)/ - come again soon!   */
} /*                                             /| |
                                                  / \                        */
