#define _GNU_SOURCE

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static unsigned long parse_id(const char *text, const char *label)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || !*text || *end || value > UINT_MAX) {
		fprintf(stderr, "run_as: invalid %s: %s\n", label, text);
		exit(2);
	}
	return value;
}

int main(int argc, char **argv)
{
	unsigned long uid_value;
	unsigned long gid_value;
	uid_t uid;
	gid_t gid;

	if (argc < 4) {
		fprintf(stderr, "usage: run_as UID GID COMMAND [ARG ...]\n");
		return 2;
	}
	uid_value = parse_id(argv[1], "UID");
	gid_value = parse_id(argv[2], "GID");
	uid = (uid_t)uid_value;
	gid = (gid_t)gid_value;
	if ((unsigned long)uid != uid_value || (unsigned long)gid != gid_value) {
		fprintf(stderr, "run_as: UID or GID is outside the platform range\n");
		return 2;
	}
	if (setgroups(0, NULL) || setgid(gid) || setuid(uid)) {
		perror("run_as: cannot drop privileges");
		return 1;
	}
	execvp(argv[3], &argv[3]);
	perror("run_as: cannot execute command");
	return 127;
}
