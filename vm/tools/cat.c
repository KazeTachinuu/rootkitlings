// read-based cat (BusyBox cat uses sendfile, bypassing read hooks)
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
	char buf[4096];
	ssize_t n;
	int i, fd;

	if (argc < 2) {
		while ((n = read(0, buf, sizeof(buf))) > 0)
			write(1, buf, n);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		fd = open(argv[i], O_RDONLY);
		if (fd < 0)
			return 1;
		while ((n = read(fd, buf, sizeof(buf))) > 0)
			write(1, buf, n);
		close(fd);
	}
	return 0;
}
