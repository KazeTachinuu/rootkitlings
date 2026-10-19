// SPDX-License-Identifier: GPL-2.0
// Call mkdir(2) to trigger covert escalation, then report uid.
// With --self-target PREFIX: use PREFIX.<pid> as the mkdir path.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *path = NULL;
	const char *self_prefix = NULL;
	char self_path[256];
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--self-target") == 0 && i + 1 < argc)
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
			"usage: %s [--self-target PREFIX] [PATH]\n",
			argv[0]);
		return 1;
	}
	mkdir(path, 0755);
	printf("%u\n", getuid());
	return 0;
}
