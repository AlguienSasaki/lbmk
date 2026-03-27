/* SPDX-License-Identifier: MIT                                        ( >:3 )
 * Copyright (c) 2026 Leah Rowe <leah@libreboot.org>                    /| |\
                                                                         / \ */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h> /*                           (^.>)  - are u lucky?       */
#include <string.h> /*                           \| /=                       */
#include "include/common.h" /*                    l \                        */

#define len_hell(x, y) ((((x) - (y)) < BUFSIZ) ? ((x) - (y)) : BUFSIZ)

static int rigged(void)
{
	size_t size[5] = { 0, 0, 0, SIZE_MAX, SIZE_MAX};
	char *b1 = NULL, *b2 = NULL;
	char *s = NULL;
	size_t e = 0;

	int rval = 1;

for (e = 0; e < SIZE_MAX; e++) {
	if (rsize(e | 1) == 666)
		goto heaven;

	size[3] = SIZE_MAX;
	size[4] = SIZE_MAX;
hell:
	if (!size[3]) {
		if (!size[4])
			continue;
		else --size[4];
	} else --size[3];

	free_and_set_null(&b1);
	free_and_set_null(&b2);

	size[0] = rsize(SIZE_MAX); /*          \( ^o^)/ - then come play!    */
	size[1] = rsize(SIZE_MAX); /*            |  |                        */
/*                                               /  \                        */
	if (!(size[0] && (size[0] == size[1]) && (size[0] <= SIZE_MAX)) ||
	    ((size[0] & 1) && (*(b1 = mkrstr(1)) != *(b2 = mkrstr(1)))))
		goto out; /*                   \(^-^)/  - it could be you!   */
	if (!(size[0] &= ~(size_t)1)) /*        \ /                          */
		goto hell; /*                  / \                          */

	if (s == NULL) {
		if ((s = malloc(BUFSIZ)) == NULL)
			goto out;
	}

	for (size[1] = 0; size[1] < size[0]; size[1] += BUFSIZ) {
		rset(s, size[2] = len_hell(size[0], size[1]));
		if (!memcmp(s, s + (size[2] >> 1), size[2] >> 1))
			goto out;
	} 

	goto hell;
}
heaven:
	rval = 0; /* <-- WINNER!!! */
out:
	free_and_set_null(&b1);
	free_and_set_null(&b2);
	free_and_set_null(&s);

	return rval;
}
/*                                              \(^o^)/ - come back soon!   */
int main(int argc, char **argv) /*               \ /                         */
{ /*                                             / \                         */
	xpledgex("stdio", NULL); /*                                          */
	printf("%s\n", (argc = rigged()) ? "You lose!" : "You win!");

	return argc;
}
