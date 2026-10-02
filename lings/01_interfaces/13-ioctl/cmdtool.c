// SPDX-License-Identifier: GPL-2.0
// Userspace companion for ling 13: sends ioctl commands to /dev/hello
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define HELLO_SET_MSG _IOW('H', 1, char[64])
#define HELLO_GET_MSG _IOR('H', 2, char[64])

int main(int argc, char **argv)
{
	int fd;
	char buf[64];

	if (argc < 2) {
		fprintf(stderr, "usage: %s set <msg> | get\n", argv[0]);
		return 1;
	}

	fd = open("/dev/hello", O_RDWR);
	if (fd < 0) {
		perror("/dev/hello");
		return 1;
	}

	if (strcmp(argv[1], "set") == 0 && argc >= 3) {
		memset(buf, 0, sizeof(buf));
		strncpy(buf, argv[2], sizeof(buf) - 1);
		if (ioctl(fd, HELLO_SET_MSG, buf) < 0) {
			perror("ioctl SET");
			close(fd);
			return 1;
		}
	} else if (strcmp(argv[1], "get") == 0) {
		memset(buf, 0, sizeof(buf));
		if (ioctl(fd, HELLO_GET_MSG, buf) < 0) {
			perror("ioctl GET");
			close(fd);
			return 1;
		}
		printf("%s\n", buf);
	} else {
		fprintf(stderr, "usage: %s set <msg> | get\n", argv[0]);
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}
