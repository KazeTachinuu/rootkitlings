// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *path;
	const char *word;
	ssize_t written;
	int descriptor;

	if (argc != 3) {
		fprintf(stderr, "usage: %s PATH COMMAND\n", argv[0]);
		return 2;
	}
	path = argv[1];
	word = argv[2];
	descriptor = open(path, O_WRONLY);
	if (descriptor < 0) {
		perror("open");
		return 1;
	}

	written = write(descriptor, word, strlen(word));
	if (written < 0) {
		perror("write");
		close(descriptor);
		return 1;
	}
	if ((size_t)written != strlen(word)) {
		fprintf(stderr, "short write: %zd\n", written);
		close(descriptor);
		return 1;
	}

	close(descriptor);
	printf("uid=%u euid=%u\n", (unsigned int)getuid(),
	       (unsigned int)geteuid());
	return 0;
}
