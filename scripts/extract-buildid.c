// SPDX-License-Identifier: GPL-2.0

/*
 * Formats buildid into following macros:
 *
 * #define LINUX_BUILDID_DATA "\x6c\x41\x0f\xea\xa9\x5d\x46 ...
 * #define LINUX_BUILDID_SIZE 20
 *
 */
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	char *id;
	int len, i;

	if (argc != 2) {
		fprintf(stderr, "usage: %s buildid\n", argv[0]);
		return -1;
	}

	id  = argv[1];
	len = strlen(id);

	printf("#ifndef _LINUX_BUILDID_H\n");
	printf("#define _LINUX_BUILDID_H\n");
	printf("\n");

	printf("#define LINUX_BUILDID_DATA \"");

	for (i = 0; i < len; i += 2)
		printf("\\x%c%c", id[i], id[i + 1]);

	printf("\"\n");

	printf("#define LINUX_BUILDID_SIZE %u\n", len / 2);

	printf("\n");
	printf("#endif /* _LINUX_BUILDID_H */\n");
	return 0;
}
