// SPDX-License-Identifier: GPL-2.0
// Verify the numeric errno returned when a module's initialization fails.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int fd;
	int saved_errno;

	if (argc != 2) {
		fprintf(stderr, "usage: %s MODULE\n", argv[0]);
		return 2;
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

	if (saved_errno != ECANCELED) {
		fprintf(stderr, "finit_module errno=%d, expected ECANCELED=%d\n",
			saved_errno, ECANCELED);
		return 1;
	}

	printf("finit_module errno=ECANCELED (%d)\n", ECANCELED);
	return 0;
}
