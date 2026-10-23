// SPDX-License-Identifier: GPL-2.0
// Call mkdir(2) to trigger covert escalation, then report uid.
// With --self-target PREFIX: use PREFIX.<pid> as the mkdir path.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *path = NULL;
	const char *self_prefix = NULL;
	char self_path[256];
	int expected_errno = -1;
	int mkdir_result;
	int mkdir_errno;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--self-target") == 0 && i + 1 < argc)
			self_prefix = argv[++i];
		else if (strcmp(argv[i], "--expect-errno") == 0 && i + 1 < argc)
			expected_errno = atoi(argv[++i]);
		else if (!path)
			path = argv[i];
	}
	if (self_prefix) {
		snprintf(self_path, sizeof(self_path), "%s.%d", self_prefix,
			 (int)getpid());
		path = self_path;
	}
	if (!path) {
		fprintf(stderr,
			"usage: %s [--self-target PREFIX] [--expect-errno N] "
			"[PATH]\n",
			argv[0]);
		return 1;
	}
	errno = 0;
	mkdir_result = mkdir(path, 0755);
	mkdir_errno = errno;
	if (expected_errno == 0 && mkdir_result != 0) {
		fprintf(stderr, "mkdir %s failed: %s\n", path,
			strerror(mkdir_errno));
		return 1;
	}
	if (expected_errno > 0 &&
	    (mkdir_result != -1 || mkdir_errno != expected_errno)) {
		fprintf(stderr,
			"mkdir %s returned errno=%d, expected errno=%d\n", path,
			mkdir_errno, expected_errno);
		return 1;
	}
	printf("%u\n", getuid());
	return 0;
}
