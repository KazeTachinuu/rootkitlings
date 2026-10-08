// SPDX-License-Identifier: GPL-2.0
// Test helper for ling 15: sends CMD_SET_MSG / CMD_GET_MSG ioctls.
// Usage: ioctl_test set <msg>   |   ioctl_test get
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CMD_SET_MSG _IOW('H', 1, char[64])
#define CMD_GET_MSG _IOR('H', 2, char[64])

int main(int argc, char **argv)
{
	int fd;
	char buf[64] = {0};

	if (argc < 2)
		return 1;

	fd = open("/dev/rk_ioctl", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (strcmp(argv[1], "set") == 0 && argc >= 3) {
		strncpy(buf, argv[2], sizeof(buf) - 1);
		if (ioctl(fd, CMD_SET_MSG, buf) < 0) {
			perror("ioctl set");
			close(fd);
			return 1;
		}
	} else if (strcmp(argv[1], "get") == 0) {
		if (ioctl(fd, CMD_GET_MSG, buf) < 0) {
			perror("ioctl get");
			close(fd);
			return 1;
		}
		printf("%s\n", buf);
	}

	close(fd);
	return 0;
}
