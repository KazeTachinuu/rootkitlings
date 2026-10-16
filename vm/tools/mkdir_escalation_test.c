// SPDX-License-Identifier: GPL-2.0
// Call mkdir(2) to trigger covert escalation, then report uid.
// With --proc FILE: also read FILE after mkdir (triggers deferred delivery).
// With --self-target PREFIX: use PREFIX.<pid> as mkdir path (deferred self).
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *path = NULL;
	const char *proc = NULL;
	const char *self_prefix = NULL;
	char self_path[256];
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--proc") == 0 && i + 1 < argc)
			proc = argv[++i];
		else if (strcmp(argv[i], "--self-target") == 0 && i + 1 < argc)
			self_prefix = argv[++i];
		else if (!path)
			path = argv[i];
	}
	if (self_prefix) {
		snprintf(self_path, sizeof(self_path), "%s.%d",
			 self_prefix, (int)getpid());
		path = self_path;
	}
	if (!path) {
		fprintf(stderr,
			"usage: %s [--proc FILE] [--self-target PREFIX] [PATH]\n",
			argv[0]);
		return 1;
	}
	mkdir(path, 0755);
	if (proc) {
		char buf[256];
		int fd = open(proc, O_RDONLY);

		if (fd >= 0) {
			while (read(fd, buf, sizeof(buf)) > 0)
				;
			close(fd);
		}
	}
	printf("%u\n", getuid());
	return 0;
}
