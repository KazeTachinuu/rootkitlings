// SPDX-License-Identifier: GPL-2.0
// Compare a Rootkitlings credential record with independent procfs views.
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RECORD_CAPACITY 1024U
#define STATUS_CAPACITY 8192U
#define TEST_NAME "cred-snapshot"

struct credential_view {
	unsigned int uids[4];
	unsigned int gids[4];
	unsigned long long permitted;
	unsigned long long effective;
	bool initial_userns;
};

static ssize_t read_retry(int fd, void *buffer, size_t length)
{
	ssize_t count;

	do {
		count = read(fd, buffer, length);
	} while (count < 0 && errno == EINTR);
	return count;
}

static int close_checked(int fd, const char *label)
{
	if (close(fd) < 0) {
		fprintf(stderr, "credential_snapshot_test: close %s: %s\n",
			label, strerror(errno));
		return -1;
	}
	return 0;
}

static int read_file(const char *path, char *buffer, size_t capacity,
		     size_t *length)
{
	size_t used = 0;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "credential_snapshot_test: open %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	while (used + 1 < capacity) {
		ssize_t count = read_retry(fd, buffer + used, capacity - used - 1);

		if (count < 0) {
			fprintf(stderr, "credential_snapshot_test: read %s: %s\n",
				path, strerror(errno));
			close(fd);
			return -1;
		}
		if (count == 0)
			break;
		used += (size_t)count;
	}
	if (used + 1 == capacity) {
		char byte;
		ssize_t count = read_retry(fd, &byte, 1);

		if (count != 0) {
			fprintf(stderr,
				"credential_snapshot_test: %s exceeds %zu bytes\n",
				path, capacity - 1);
			close(fd);
			return -1;
		}
	}
	if (close_checked(fd, path) < 0)
		return -1;
	buffer[used] = '\0';
	*length = used;
	return 0;
}

static const char *find_field(const char *record, const char *name)
{
	const size_t name_length = strlen(name);
	const char *cursor = record;

	while ((cursor = strstr(cursor, name)) != NULL) {
		if ((cursor == record || cursor[-1] == '\n') &&
		    cursor[name_length] == '\t')
			return cursor + name_length + 1;
		cursor += name_length;
	}
	return NULL;
}

static int parse_four_ids(const char *record, const char *name,
			  unsigned int values[4])
{
	const char *field = find_field(record, name);
	int consumed = 0;

	if (!field || sscanf(field, "%u\t%u\t%u\t%u%n", &values[0],
			    &values[1], &values[2], &values[3], &consumed) != 4 ||
	    field[consumed] != '\n') {
		fprintf(stderr,
			"credential_snapshot_test: malformed %s field in /proc/thread-self/status\n",
			name);
		return -1;
	}
	return 0;
}

static int parse_capability(const char *record, const char *name,
			    unsigned long long *value)
{
	const char *field = find_field(record, name);
	char hex[17];
	char *end;
	size_t index;

	if (!field)
		goto malformed;
	for (index = 0; index < 16; index++) {
		const char byte = field[index];

		if (!((byte >= '0' && byte <= '9') ||
		      (byte >= 'a' && byte <= 'f')))
			goto malformed;
		hex[index] = byte;
	}
	if (field[16] != '\n')
		goto malformed;
	hex[16] = '\0';
	errno = 0;
	*value = strtoull(hex, &end, 16);
	if (errno != 0 || *end != '\0')
		goto malformed;
	return 0;

malformed:
	fprintf(stderr,
		"credential_snapshot_test: malformed %s field in /proc/thread-self/status\n",
		name);
	return -1;
}

static int classify_current_userns(bool *initial)
{
	char mapping[256];
	unsigned long long inside;
	unsigned long long outside;
	unsigned long long count;
	char trailing;
	size_t length;

	if (read_file("/proc/thread-self/uid_map", mapping, sizeof(mapping),
		      &length) < 0)
		return -1;
	(void)length;
	if (sscanf(mapping, "%llu %llu %llu%c", &inside, &outside, &count,
		   &trailing) != 4 || trailing != '\n') {
		fprintf(stderr,
			"credential_snapshot_test: expected one UID mapping extent\n");
		return -1;
	}
	*initial = inside == 0 && outside == 0 && count == 4294967295ULL;
	return 0;
}

static int observe_current(struct credential_view *view)
{
	char status[STATUS_CAPACITY];
	size_t length;

	if (read_file("/proc/thread-self/status", status, sizeof(status),
		      &length) < 0)
		return -1;
	(void)length;
	if (parse_four_ids(status, "Uid:", view->uids) < 0 ||
	    parse_four_ids(status, "Gid:", view->gids) < 0 ||
	    parse_capability(status, "CapPrm:", &view->permitted) < 0 ||
	    parse_capability(status, "CapEff:", &view->effective) < 0 ||
	    classify_current_userns(&view->initial_userns) < 0)
		return -1;
	return 0;
}

static int read_record(int fd, unsigned char record[RECORD_CAPACITY],
		       size_t *length)
{
	static const size_t pattern[] = { 1, 3, 2, 7, 5, 11, 13 };
	size_t used = 0;
	size_t step = 0;

	for (;;) {
		size_t request = pattern[step % (sizeof(pattern) / sizeof(pattern[0]))];
		ssize_t count;

		if (request > RECORD_CAPACITY - used)
			request = RECORD_CAPACITY - used;
		if (request == 0) {
			fprintf(stderr,
				"credential_snapshot_test: record exceeds %u bytes\n",
				RECORD_CAPACITY);
			return -1;
		}
		count = read_retry(fd, record + used, request);
		if (count < 0) {
			fprintf(stderr, "credential_snapshot_test: read record: %s\n",
				strerror(errno));
			return -1;
		}
		if (count == 0)
			break;
		used += (size_t)count;
		step++;
	}
	for (step = 0; step < 2; step++) {
		unsigned char byte;
		ssize_t count = read_retry(fd, &byte, 1);

		if (count != 0) {
			fprintf(stderr,
				"credential_snapshot_test: record returned data after EOF\n");
			return -1;
		}
	}
	*length = used;
	return 0;
}

static int compare_descriptor(int fd, const unsigned int *uid_override,
			      const unsigned int *gid_override,
			      const char *phase)
{
	struct credential_view view;
	unsigned char actual[RECORD_CAPACITY];
	char expected[RECORD_CAPACITY];
	const unsigned int *uids;
	const unsigned int *gids;
	size_t actual_length;
	int expected_length;

	if (observe_current(&view) < 0)
		return -1;
	uids = uid_override ? uid_override : view.uids;
	gids = gid_override ? gid_override : view.gids;
	expected_length = snprintf(
		expected, sizeof(expected),
		"active=1\n"
		"reader_tid=%ld\n"
		"reader_comm=\"%s\"\n"
		"uids=%u %u %u %u\n"
		"gids=%u %u %u %u\n"
		"cap_userns=%s\n"
		"cap_permitted=%016llx\n"
		"cap_effective=%016llx\n",
		(long)syscall(SYS_gettid), TEST_NAME,
		uids[0], uids[1], uids[2], uids[3],
		gids[0], gids[1], gids[2], gids[3],
		view.initial_userns ? "initial" : "nested",
		view.permitted, view.effective);
	if (expected_length < 0 || (size_t)expected_length >= sizeof(expected)) {
		fprintf(stderr,
			"credential_snapshot_test: expected record overflow\n");
		return -1;
	}
	if (read_record(fd, actual, &actual_length) < 0)
		return -1;
	if (actual_length != (size_t)expected_length ||
	    memcmp(actual, expected, (size_t)expected_length) != 0) {
		fprintf(stderr,
			"credential_snapshot_test: %s record differs\nexpected:\n%sactual:\n%.*s",
			phase, expected, (int)actual_length, (const char *)actual);
		return -1;
	}
	return 0;
}

static int open_and_compare(const char *path, const char *phase)
{
	int result;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "credential_snapshot_test: open %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	result = compare_descriptor(fd, NULL, NULL, phase);
	if (close_checked(fd, path) < 0)
		return -1;
	return result;
}

static int clear_groups(void)
{
	if (setgroups(0, NULL) < 0) {
		fprintf(stderr, "credential_snapshot_test: setgroups: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static int become_nobody(void)
{
	if (clear_groups() < 0 || setresgid(65534, 65534, 65534) < 0 ||
	    setresuid(65534, 65534, 65534) < 0) {
		fprintf(stderr, "credential_snapshot_test: become nobody: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static int install_distinct_roles(void)
{
	if (clear_groups() < 0 || setresgid(2001, 0, 2002) < 0 ||
	    setresuid(1001, 0, 1002) < 0) {
		fprintf(stderr,
			"credential_snapshot_test: install distinct IDs: %s\n",
			strerror(errno));
		return -1;
	}
	(void)setfsgid(2003);
	if ((gid_t)setfsgid((gid_t)-1) != 2003) {
		fprintf(stderr,
			"credential_snapshot_test: filesystem GID did not become 2003\n");
		return -1;
	}
	(void)setfsuid(1003);
	if ((uid_t)setfsuid((uid_t)-1) != 1003) {
		fprintf(stderr,
			"credential_snapshot_test: filesystem UID did not become 1003\n");
		return -1;
	}
	return 0;
}

static int write_exact_file(const char *path, const char *value)
{
	const size_t length = strlen(value);
	ssize_t count;
	int fd = open(path, O_WRONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "credential_snapshot_test: open %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	do {
		count = write(fd, value, length);
	} while (count < 0 && errno == EINTR);
	if (count != (ssize_t)length) {
		if (count < 0)
			fprintf(stderr, "credential_snapshot_test: write %s: %s\n",
				path, strerror(errno));
		else
			fprintf(stderr,
				"credential_snapshot_test: short write to %s\n", path);
		close(fd);
		return -1;
	}
	return close_checked(fd, path);
}

static int enter_nobody_userns(void)
{
	if (become_nobody() < 0)
		return -1;
	if (prctl(PR_SET_DUMPABLE, 1, 0UL, 0UL, 0UL) < 0) {
		fprintf(stderr,
			"credential_snapshot_test: restore dumpability: %s\n",
			strerror(errno));
		return -1;
	}
	if (unshare(CLONE_NEWUSER) < 0) {
		fprintf(stderr, "credential_snapshot_test: unshare user namespace: %s\n",
			strerror(errno));
		return -1;
	}
	if (write_exact_file("/proc/self/setgroups", "deny\n") < 0 ||
	    write_exact_file("/proc/self/uid_map", "0 65534 1\n") < 0 ||
	    write_exact_file("/proc/self/gid_map", "0 65534 1\n") < 0)
		return -1;
	return 0;
}

static int send_descriptor(int socket_fd, int descriptor)
{
	char control[CMSG_SPACE(sizeof(descriptor))];
	char payload = 'F';
	struct iovec vector = { .iov_base = &payload, .iov_len = 1 };
	struct msghdr message;
	struct cmsghdr *header;

	memset(&message, 0, sizeof(message));
	memset(control, 0, sizeof(control));
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = control;
	message.msg_controllen = sizeof(control);
	header = CMSG_FIRSTHDR(&message);
	header->cmsg_level = SOL_SOCKET;
	header->cmsg_type = SCM_RIGHTS;
	header->cmsg_len = CMSG_LEN(sizeof(descriptor));
	memcpy(CMSG_DATA(header), &descriptor, sizeof(descriptor));
	message.msg_controllen = CMSG_SPACE(sizeof(descriptor));
	if (sendmsg(socket_fd, &message, 0) != 1) {
		fprintf(stderr,
			"credential_snapshot_test: send descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static int receive_descriptor(int socket_fd)
{
	char control[CMSG_SPACE(sizeof(int))];
	char payload;
	struct iovec vector = { .iov_base = &payload, .iov_len = 1 };
	struct msghdr message;
	struct cmsghdr *header;
	int descriptor;

	memset(&message, 0, sizeof(message));
	memset(control, 0, sizeof(control));
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = control;
	message.msg_controllen = sizeof(control);
	if (recvmsg(socket_fd, &message, 0) != 1) {
		fprintf(stderr,
			"credential_snapshot_test: receive descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	header = CMSG_FIRSTHDR(&message);
	if (!header || header->cmsg_level != SOL_SOCKET ||
	    header->cmsg_type != SCM_RIGHTS ||
	    header->cmsg_len != CMSG_LEN(sizeof(descriptor))) {
		fprintf(stderr,
			"credential_snapshot_test: malformed descriptor message\n");
		return -1;
	}
	memcpy(&descriptor, CMSG_DATA(header), sizeof(descriptor));
	return descriptor;
}

static int read_unsigned_file(const char *path, unsigned int *value)
{
	char buffer[64];
	char trailing;
	size_t length;

	if (read_file(path, buffer, sizeof(buffer), &length) < 0)
		return -1;
	(void)length;
	if (sscanf(buffer, "%u%c", value, &trailing) != 2 || trailing != '\n') {
		fprintf(stderr, "credential_snapshot_test: malformed %s\n", path);
		return -1;
	}
	return 0;
}

static int verify_cross_namespace_descriptor(const char *path)
{
	unsigned int overflow_uid;
	unsigned int overflow_gid;
	unsigned int uids[4];
	unsigned int gids[4];
	int sockets[2];
	pid_t child;
	int descriptor;
	int status;
	int result;

	if (read_unsigned_file("/proc/sys/kernel/overflowuid", &overflow_uid) < 0 ||
	    read_unsigned_file("/proc/sys/kernel/overflowgid", &overflow_gid) < 0)
		return -1;
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) < 0) {
		fprintf(stderr, "credential_snapshot_test: socketpair: %s\n",
			strerror(errno));
		return -1;
	}
	child = fork();
	if (child < 0) {
		fprintf(stderr, "credential_snapshot_test: fork: %s\n",
			strerror(errno));
		close(sockets[0]);
		close(sockets[1]);
		return -1;
	}
	if (child == 0) {
		int child_fd;
		int child_result;

		close(sockets[0]);
		if (enter_nobody_userns() < 0)
			_exit(1);
		child_fd = open(path, O_RDONLY | O_CLOEXEC);
		if (child_fd < 0) {
			fprintf(stderr,
				"credential_snapshot_test: nested open %s: %s\n",
				path, strerror(errno));
			_exit(1);
		}
		child_result = send_descriptor(sockets[1], child_fd);
		close(child_fd);
		close(sockets[1]);
		_exit(child_result == 0 ? 0 : 1);
	}
	close(sockets[1]);
	descriptor = receive_descriptor(sockets[0]);
	close(sockets[0]);
	if (waitpid(child, &status, 0) != child) {
		fprintf(stderr, "credential_snapshot_test: waitpid: %s\n",
			strerror(errno));
		if (descriptor >= 0)
			close(descriptor);
		return -1;
	}
	if (descriptor < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr,
			"credential_snapshot_test: nested descriptor sender failed\n");
		if (descriptor >= 0)
			close(descriptor);
		return -1;
	}
	for (size_t index = 0; index < 4; index++) {
		uids[index] = overflow_uid;
		gids[index] = overflow_gid;
	}
	result = compare_descriptor(descriptor, uids, gids,
				    "cross-namespace descriptor");
	if (close_checked(descriptor, "cross-namespace descriptor") < 0)
		return -1;
	return result;
}

static int verify_after_open(const char *path)
{
	int result;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "credential_snapshot_test: open before ID change: %s\n",
			strerror(errno));
		return -1;
	}
	if (become_nobody() < 0) {
		close(fd);
		return -1;
	}
	result = compare_descriptor(fd, NULL, NULL, "credential change after open");
	if (close_checked(fd, path) < 0)
		return -1;
	return result;
}

int main(int argc, char **argv)
{
	int result;

	if (argc != 3) {
		fprintf(stderr,
			"usage: %s current|roles|after-open|cross-ns PATH\n",
			argv[0]);
		return 2;
	}
	if (prctl(PR_SET_NAME, TEST_NAME, 0UL, 0UL, 0UL) < 0) {
		fprintf(stderr, "credential_snapshot_test: PR_SET_NAME: %s\n",
			strerror(errno));
		return 1;
	}
	if (strcmp(argv[1], "current") == 0)
		result = open_and_compare(argv[2], "current credentials");
	else if (strcmp(argv[1], "roles") == 0) {
		if (install_distinct_roles() < 0)
			return 1;
		result = open_and_compare(argv[2], "distinct credential roles");
	} else if (strcmp(argv[1], "after-open") == 0) {
		result = verify_after_open(argv[2]);
	} else if (strcmp(argv[1], "cross-ns") == 0) {
		result = verify_cross_namespace_descriptor(argv[2]);
	} else {
		fprintf(stderr, "credential_snapshot_test: unknown mode: %s\n",
			argv[1]);
		return 2;
	}
	if (result < 0)
		return 1;
	printf("credential_snapshot_test: %s verified\n", argv[1]);
	return 0;
}
