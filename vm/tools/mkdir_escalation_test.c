// SPDX-License-Identifier: GPL-2.0
// Call mkdir(2) to trigger covert escalation, then report uid.
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *path;

	if (argc < 2) {
		fprintf(stderr, "usage: %s PATH\n", argv[0]);
		return 1;
	}
	path = argv[1];
	mkdir(path, 0755);
	printf("%u\n", getuid());
	return 0;
}
