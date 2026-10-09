// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	char *end = NULL;
	long expected;
	int error;
	int fd;

	if (argc != 4) {
		fprintf(stderr, "usage: %s MODULE EXPECTED_ERRNO PARAMETERS\n", argv[0]);
		return 2;
	}
	errno = 0;
	expected = strtol(argv[2], &end, 10);
	if (errno || end == argv[2] || *end || expected <= 0 || expected > INT_MAX) {
		fprintf(stderr, "invalid errno: %s\n", argv[2]);
		return 2;
	}
	fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror("open module");
		return 2;
	}
	errno = 0;
	if (syscall(SYS_finit_module, fd, argv[3], 0) == 0) {
		fprintf(stderr, "module loaded unexpectedly\n");
		close(fd);
		return 1;
	}
	error = errno;
	close(fd);
	if (error != expected) {
		fprintf(stderr, "finit_module errno=%d, expected errno=%ld\n",
			error, expected);
		return 1;
	}
	printf("finit_module returned expected errno=%d\n", error);
	return 0;
}
