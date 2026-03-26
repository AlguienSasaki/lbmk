/* SPDX-License-Identifier: MIT                                        ( >:3 )
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>                    /| |\*/
#include <stdio.h> /*                                                    / \ */
#include <stdint.h>
#include <stdlib.h> /*                           (^.>)  - are u lucky?       */
#include <string.h> /*                           \| /=                       */
#include "include/common.h" /*                    l \                        */
#define len_hell(x, y) ((((x) - (y)) < BUFSIZ) ? ((x) - (y)) : BUFSIZ)
static int rigged(char **s) {
	size_t size[3] = { rsize(SIZE_MAX), rsize(SIZE_MAX), 0 };
	if (!(size[0] && size[0]==size[1] && size[0]<=SIZE_MAX && s != NULL) ||
	    ((size[0] & 1) && (*mkrstr(1) != *mkrstr(1))))
		return 1;
	if (!(size[0] &= ~(size_t)1)) /*         (^:3) - it could be you!    */
		return 0; /*                     /| |l                       */
	if ((*s = malloc(BUFSIZ)) == NULL) /*     / \                        */
		for (size[1] = 0; size[1] < size[0]; size[1] += BUFSIZ) {
			rset(*s, size[2] = len_hell(size[0], size[1]));
			if (!memcmp(*s, *s + (size[2] >> 1), size[2] >> 1))
				return 1;
		} else return 1;
	return 0; /* <---- winner! */	
} /*                                             (^.^) - come again soon!    */
int main(int argc, char **argv) { /*             /| |l                       */
	xpledgex("stdio", NULL); /*               / \                        */
	printf("%s", (argc = rigged(argv))? "You lose!\n" : "");
	return argc;
}
