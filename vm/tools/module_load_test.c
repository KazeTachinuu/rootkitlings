// SPDX-License-Identifier: GPL-2.0
// Verify the numeric errno returned when a module's initialization fails.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int fd;
	int expected_errno = ECANCELED;
	int saved_errno;
	char *end;
	long parsed;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s MODULE [EXPECTED_ERRNO]\n", argv[0]);
		return 2;
	}
	if (argc == 3) {
		errno = 0;
		end = NULL;
		parsed = strtol(argv[2], &end, 10);
		if (errno != 0 || end == argv[2] || *end != '\0' || parsed <= 0 ||
		    parsed > 4095) {
			fprintf(stderr, "invalid expected errno: %s\n", argv[2]);
			return 2;
		}
		expected_errno = (int)parsed;
	}

	fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror("open module");
		return 2;
	}

	errno = 0;
	if (syscall(SYS_finit_module, fd, "", 0) == 0) {
		fprintf(stderr, "module loaded unexpectedly\n");
		close(fd);
		return 1;
	}
	saved_errno = errno;
	close(fd);

	if (saved_errno != expected_errno) {
		fprintf(stderr, "finit_module errno=%d, expected errno=%d\n",
			saved_errno, expected_errno);
		return 1;
	}

	printf("finit_module returned expected errno=%d\n", expected_errno);
	return 0;
}
