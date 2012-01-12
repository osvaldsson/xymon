/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Utility program to define endian-ness of the target system.                */
/*                                                                            */
/* Copyright (C) 2006-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: test-endianness.c 6712 2011-07-31 21:01:52Z storner $";

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	unsigned int c;
	unsigned char cbuf[sizeof(c)];
	int i;
	int outform = 1;

	if ((argc > 1) && (strcmp(argv[1], "--configh") == 0)) outform = 0;

	for (i=0; (i < sizeof(c)); i++) {
		cbuf[i] = (i % 2);
	}

	memcpy(&c, cbuf, sizeof(c));

	if (c == 65537) {
		/* Big endian */
		if (outform == 0)
			printf("#ifndef XYMON_BIG_ENDIAN\n#define XYMON_BIG_ENDIAN\n#endif\n");
		else
			printf(" -DXYMON_BIG_ENDIAN");
	}
	else if (c == 16777472) {
		/* Little endian */
		if (outform == 0)
			printf("#ifndef XYMON_LITTLE_ENDIAN\n#define XYMON_LITTLE_ENDIAN\n#endif\n");
		else
			printf(" -DXYMON_LITTLE_ENDIAN");
	}
	else {
		fprintf(stderr, "UNKNOWN ENDIANNESS! testvalue is %u\n", c);
	}

	fflush(stdout);
	return 0;
}

