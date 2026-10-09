// SPDX-License-Identifier: GPL-2.0
// Verify that a proc write replaces only the writing task's credentials.
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GROUP_CAPACITY 64
#define RECORD_CAPACITY 2048
#define STATUS_CAPACITY 8192
#define TEST_NAME "cred-trigger"

enum capability_field {
	CAP_INHERITABLE,
	CAP_PERMITTED,
	CAP_EFFECTIVE,
	CAP_BOUNDING,
	CAP_AMBIENT,
	CAP_FIELD_COUNT,
};

struct credential_view {
	unsigned int uids[4];
	unsigned int gids[4];
	unsigned long long capabilities[CAP_FIELD_COUNT];
	struct stat user_namespace;
	unsigned int securebits;
	gid_t groups[GROUP_CAPACITY];
	int group_count;
};

struct observer_context {
	pthread_barrier_t ready;
	pthread_barrier_t finished;
	struct credential_view before;
	struct credential_view after;
	int result;
};

static const char *const capability_names[CAP_FIELD_COUNT] = {
	[CAP_INHERITABLE] = "CapInh:",
	[CAP_PERMITTED] = "CapPrm:",
	[CAP_EFFECTIVE] = "CapEff:",
	[CAP_BOUNDING] = "CapBnd:",
	[CAP_AMBIENT] = "CapAmb:",
};

static void usage(const char *program)
{
	fprintf(stderr,
		"usage:\n"
		"  %s PATH COMMAND\n"
		"  %s unchanged PATH COMMAND ERRNO\n"
		"  %s replace PATH CHROOT_DIR\n"
		"  %s thread-scope PATH\n"
		"  %s outcome PATH\n",
		program, program, program, program, program);
}

static ssize_t read_retry(int descriptor, void *buffer, size_t length)
{
	ssize_t count;

	do {
		count = read(descriptor, buffer, length);
	} while (count < 0 && errno == EINTR);
	return count;
}

static int read_file(const char *path, char *buffer, size_t capacity,
		     size_t *length)
{
	size_t used = 0;
	int descriptor = open(path, O_RDONLY | O_CLOEXEC);

	if (descriptor < 0) {
		fprintf(stderr, "cred_trigger_test: open %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	while (used + 1 < capacity) {
		ssize_t count = read_retry(descriptor, buffer + used,
					   capacity - used - 1);

		if (count < 0) {
			fprintf(stderr, "cred_trigger_test: read %s: %s\n", path,
				strerror(errno));
			close(descriptor);
			return -1;
		}
		if (count == 0)
			break;
		used += (size_t)count;
	}
	if (used + 1 == capacity) {
		char byte;

		if (read_retry(descriptor, &byte, 1) != 0) {
			fprintf(stderr, "cred_trigger_test: %s is too large\n", path);
			close(descriptor);
			return -1;
		}
	}
	if (close(descriptor) < 0) {
		fprintf(stderr, "cred_trigger_test: close %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	buffer[used] = '\0';
	*length = used;
	return 0;
}

static const char *find_status_field(const char *status, const char *name)
{
	const size_t name_length = strlen(name);
	const char *cursor = status;

	while ((cursor = strstr(cursor, name)) != NULL) {
		if ((cursor == status || cursor[-1] == '\n') &&
		    cursor[name_length] == '\t')
			return cursor + name_length + 1;
		cursor += name_length;
	}
	return NULL;
}

static int parse_four_ids(const char *status, const char *name,
			  unsigned int values[4])
{
	const char *field = find_status_field(status, name);
	int consumed = 0;

	if (!field || sscanf(field, "%u\t%u\t%u\t%u%n", &values[0],
			    &values[1], &values[2], &values[3], &consumed) != 4 ||
	    field[consumed] != '\n') {
		fprintf(stderr, "cred_trigger_test: malformed %s status field\n",
			name);
		return -1;
	}
	return 0;
}

static int parse_capability(const char *status, const char *name,
			    unsigned long long *value)
{
	const char *field = find_status_field(status, name);
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
	fprintf(stderr, "cred_trigger_test: malformed %s status field\n", name);
	return -1;
}

static int observe_current(struct credential_view *view)
{
	char status[STATUS_CAPACITY];
	size_t length;
	int securebits;
	int count;
	size_t index;

	memset(view, 0, sizeof(*view));
	if (read_file("/proc/thread-self/status", status, sizeof(status),
		      &length) < 0)
		return -1;
	(void)length;
	if (parse_four_ids(status, "Uid:", view->uids) < 0 ||
	    parse_four_ids(status, "Gid:", view->gids) < 0)
		return -1;
	for (index = 0; index < CAP_FIELD_COUNT; index++) {
		if (parse_capability(status, capability_names[index],
				     &view->capabilities[index]) < 0)
			return -1;
	}
	securebits = prctl(PR_GET_SECUREBITS, 0UL, 0UL, 0UL, 0UL);
	if (securebits < 0) {
		fprintf(stderr, "cred_trigger_test: PR_GET_SECUREBITS: %s\n",
			strerror(errno));
		return -1;
	}
	view->securebits = (unsigned int)securebits;
	if (stat("/proc/thread-self/ns/user", &view->user_namespace) < 0) {
		fprintf(stderr, "cred_trigger_test: stat current user namespace: %s\n",
			strerror(errno));
		return -1;
	}
	count = getgroups(0, NULL);
	if (count < 0 || count > GROUP_CAPACITY) {
		fprintf(stderr, "cred_trigger_test: unsupported group count: %d\n",
			count);
		return -1;
	}
	view->group_count = count;
	if (count != 0 && getgroups(count, view->groups) != count) {
		fprintf(stderr, "cred_trigger_test: getgroups: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static bool same_view(const struct credential_view *left,
		      const struct credential_view *right)
{
	return memcmp(left->uids, right->uids, sizeof(left->uids)) == 0 &&
	       memcmp(left->gids, right->gids, sizeof(left->gids)) == 0 &&
	       memcmp(left->capabilities, right->capabilities,
		      sizeof(left->capabilities)) == 0 &&
	       left->user_namespace.st_dev == right->user_namespace.st_dev &&
	       left->user_namespace.st_ino == right->user_namespace.st_ino &&
	       left->securebits == right->securebits &&
	       left->group_count == right->group_count &&
	       memcmp(left->groups, right->groups,
		      (size_t)left->group_count * sizeof(left->groups[0])) == 0;
}

static int read_last_cap(unsigned int *last_cap)
{
	char buffer[64];
	char trailing;
	size_t length;

	if (read_file("/proc/sys/kernel/cap_last_cap", buffer, sizeof(buffer),
		      &length) < 0)
		return -1;
	(void)length;
	if (sscanf(buffer, "%u%c", last_cap, &trailing) != 2 ||
	    trailing != '\n' || *last_cap > 63) {
		fprintf(stderr, "cred_trigger_test: malformed cap_last_cap\n");
		return -1;
	}
	return 0;
}

static int require_nobody(const struct credential_view *view)
{
	size_t index;

	for (index = 0; index < 4; index++) {
		if (view->uids[index] != 65534 || view->gids[index] != 65534) {
			fprintf(stderr,
				"cred_trigger_test: caller is not fully UID/GID 65534\n");
			return -1;
		}
	}
	if (view->capabilities[CAP_INHERITABLE] != 0 ||
	    view->capabilities[CAP_PERMITTED] != 0 ||
	    view->capabilities[CAP_EFFECTIVE] != 0 ||
	    view->capabilities[CAP_AMBIENT] != 0) {
		fprintf(stderr,
			"cred_trigger_test: caller has unexpected capabilities\n");
		return -1;
	}
	return 0;
}

static int require_initial_credential(const struct credential_view *view)
{
	struct stat initial_namespace;
	unsigned long long full;
	unsigned int last_cap;
	size_t index;

	if (read_last_cap(&last_cap) < 0)
		return -1;
	full = last_cap == 63 ? UINT64_MAX : (1ULL << (last_cap + 1)) - 1;
	for (index = 0; index < 4; index++) {
		if (view->uids[index] != 0 || view->gids[index] != 0) {
			fprintf(stderr,
				"cred_trigger_test: replacement did not set every UID/GID role to zero\n");
			return -1;
		}
	}
	if (view->capabilities[CAP_INHERITABLE] != 0 ||
	    view->capabilities[CAP_PERMITTED] != full ||
	    view->capabilities[CAP_EFFECTIVE] != full ||
	    view->capabilities[CAP_BOUNDING] != full ||
	    view->capabilities[CAP_AMBIENT] != 0) {
		fprintf(stderr,
			"cred_trigger_test: replacement capability sets are incomplete\n");
		return -1;
	}
	if (view->securebits != 0 || view->group_count != 0) {
		fprintf(stderr,
			"cred_trigger_test: replacement securebits or groups differ from init credentials\n");
		return -1;
	}
	if (stat("/proc/1/ns/user", &initial_namespace) < 0) {
		fprintf(stderr, "cred_trigger_test: stat initial user namespace: %s\n",
			strerror(errno));
		return -1;
	}
	if (view->user_namespace.st_dev != initial_namespace.st_dev ||
	    view->user_namespace.st_ino != initial_namespace.st_ino) {
		fprintf(stderr,
			"cred_trigger_test: replacement is not anchored in the initial user namespace\n");
		return -1;
	}
	return 0;
}

static int write_command(const char *path, const char *command,
			 int *write_errno)
{
	const size_t length = strlen(command);
	ssize_t count;
	int saved_errno;
	int descriptor = open(path, O_WRONLY | O_CLOEXEC);

	if (descriptor < 0) {
		fprintf(stderr, "cred_trigger_test: open %s for write: %s\n", path,
			strerror(errno));
		return -1;
	}
	do {
		errno = 0;
		count = write(descriptor, command, length);
	} while (count < 0 && errno == EINTR);
	saved_errno = errno;
	if (close(descriptor) < 0) {
		fprintf(stderr, "cred_trigger_test: close %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	if (count >= 0 && (size_t)count != length) {
		fprintf(stderr, "cred_trigger_test: short command write\n");
		return -1;
	}
	*write_errno = count < 0 ? saved_errno : 0;
	return 0;
}

static int verify_record(const char *path, const struct credential_view *view)
{
	char actual[RECORD_CAPACITY];
	char expected[RECORD_CAPACITY];
	size_t actual_length;
	int expected_length;

	if (read_file(path, actual, sizeof(actual), &actual_length) < 0)
		return -1;
	expected_length = snprintf(
		expected, sizeof(expected),
		"active=1\n"
		"actions=enabled\n"
		"reader_tid=%ld\n"
		"reader_comm=\"%s\"\n"
		"uids=%u %u %u %u\n"
		"gids=%u %u %u %u\n"
		"cap_userns=initial\n"
		"cap_permitted=%016llx\n"
		"cap_effective=%016llx\n",
		(long)syscall(SYS_gettid), TEST_NAME,
		view->uids[0], view->uids[1], view->uids[2], view->uids[3],
		view->gids[0], view->gids[1], view->gids[2], view->gids[3],
		view->capabilities[CAP_PERMITTED],
		view->capabilities[CAP_EFFECTIVE]);
	if (expected_length < 0 || (size_t)expected_length >= sizeof(expected)) {
		fprintf(stderr, "cred_trigger_test: expected record overflow\n");
		return -1;
	}
	if (actual_length != (size_t)expected_length ||
	    memcmp(actual, expected, (size_t)expected_length) != 0) {
		fprintf(stderr,
			"cred_trigger_test: replacement record differs\nexpected:\n%sactual:\n%.*s",
			expected, (int)actual_length, actual);
		return -1;
	}
	return 0;
}

static int verify_unchanged(const char *path, const char *command,
			    int expected_errno)
{
	struct credential_view before;
	struct credential_view after;
	int write_errno;

	if (observe_current(&before) < 0 || require_nobody(&before) < 0 ||
	    write_command(path, command, &write_errno) < 0 ||
	    observe_current(&after) < 0)
		return -1;
	if (write_errno != expected_errno) {
		fprintf(stderr,
			"cred_trigger_test: command returned errno %d, expected %d\n",
			write_errno, expected_errno);
		return -1;
	}
	if (!same_view(&before, &after)) {
		fprintf(stderr,
			"cred_trigger_test: rejected command changed its caller\n");
		return -1;
	}
	return 0;
}

static int verify_writer(const char *path, const char *root_directory,
			 bool test_chroot)
{
	struct credential_view before;
	struct credential_view after;
	int write_errno;

	if (observe_current(&before) < 0 || require_nobody(&before) < 0)
		return -1;
	if (test_chroot) {
		errno = 0;
		if (chroot(root_directory) == 0 || errno != EPERM) {
			fprintf(stderr,
				"cred_trigger_test: unprivileged chroot did not fail with EPERM\n");
			return -1;
		}
	}
	if (write_command(path, "root", &write_errno) < 0)
		return -1;
	if (write_errno != 0) {
		fprintf(stderr, "cred_trigger_test: root returned errno %d\n",
			write_errno);
		return -1;
	}
	if (observe_current(&after) < 0 ||
	    require_initial_credential(&after) < 0 ||
	    verify_record(path, &after) < 0)
		return -1;
	if (test_chroot && chroot(root_directory) < 0) {
		fprintf(stderr, "cred_trigger_test: privileged chroot: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static int verify_replacement(const char *path, const char *root_directory)
{
	struct credential_view before;
	struct credential_view after;
	pid_t child;
	int status;

	if (observe_current(&before) < 0 || require_nobody(&before) < 0)
		return -1;
	child = fork();
	if (child < 0) {
		fprintf(stderr, "cred_trigger_test: fork: %s\n", strerror(errno));
		return -1;
	}
	if (child == 0)
		_exit(verify_writer(path, root_directory, true) == 0 ? 0 : 1);
	if (waitpid(child, &status, 0) != child) {
		fprintf(stderr, "cred_trigger_test: waitpid: %s\n", strerror(errno));
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "cred_trigger_test: writing child failed\n");
		return -1;
	}
	if (observe_current(&after) < 0 || !same_view(&before, &after)) {
		fprintf(stderr,
			"cred_trigger_test: unrelated parent credentials changed\n");
		return -1;
	}
	return 0;
}

static void *observe_sibling(void *argument)
{
	struct observer_context *context = argument;

	context->result = observe_current(&context->before);
	if (context->result == 0)
		context->result = require_nobody(&context->before);
	pthread_barrier_wait(&context->ready);
	pthread_barrier_wait(&context->finished);
	if (context->result == 0)
		context->result = observe_current(&context->after);
	if (context->result == 0 &&
	    !same_view(&context->before, &context->after)) {
		fprintf(stderr,
			"cred_trigger_test: existing sibling thread credentials changed\n");
		context->result = -1;
	}
	return NULL;
}

static int verify_thread_scope(const char *path)
{
	struct observer_context context;
	pthread_t observer;
	int result = -1;

	memset(&context, 0, sizeof(context));
	if (pthread_barrier_init(&context.ready, NULL, 2) != 0 ||
	    pthread_barrier_init(&context.finished, NULL, 2) != 0) {
		fprintf(stderr, "cred_trigger_test: initialize barriers\n");
		return -1;
	}
	if (pthread_create(&observer, NULL, observe_sibling, &context) != 0) {
		fprintf(stderr, "cred_trigger_test: create observer thread\n");
		goto destroy_barriers;
	}
	pthread_barrier_wait(&context.ready);
	if (context.result == 0)
		result = verify_writer(path, NULL, false);
	pthread_barrier_wait(&context.finished);
	if (pthread_join(observer, NULL) != 0) {
		fprintf(stderr, "cred_trigger_test: join observer thread\n");
		result = -1;
	} else if (context.result != 0) {
		result = -1;
	}

destroy_barriers:
	pthread_barrier_destroy(&context.finished);
	pthread_barrier_destroy(&context.ready);
	return result;
}

static int verify_outcome(const char *path)
{
	struct credential_view before;
	struct credential_view after;
	int write_errno;

	if (observe_current(&before) < 0 || require_nobody(&before) < 0 ||
	    write_command(path, "root", &write_errno) < 0 ||
	    observe_current(&after) < 0)
		return -1;
	if (write_errno == EACCES) {
		if (!same_view(&before, &after))
			return -1;
		puts("denied");
		return 0;
	}
	if (write_errno != 0 || require_initial_credential(&after) < 0)
		return -1;
	puts("root");
	return 0;
}

static int parse_errno(const char *text, int *value)
{
	char *end;
	long parsed;

	errno = 0;
	parsed = strtol(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed <= 0 ||
	    parsed > 4095)
		return -1;
	*value = (int)parsed;
	return 0;
}

static int run_legacy_trigger(const char *path, const char *command)
{
	int write_errno;

	if (write_command(path, command, &write_errno) < 0)
		return -1;
	if (write_errno != 0) {
		errno = write_errno;
		fprintf(stderr, "cred_trigger_test: write %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	printf("uid=%u euid=%u\n", (unsigned int)getuid(),
	       (unsigned int)geteuid());
	return 0;
}

int main(int argc, char **argv)
{
	int expected_errno;
	int result;

	if (argc < 3) {
		usage(argv[0]);
		return 2;
	}
	if (prctl(PR_SET_NAME, TEST_NAME, 0UL, 0UL, 0UL) < 0) {
		fprintf(stderr, "cred_trigger_test: PR_SET_NAME: %s\n",
			strerror(errno));
		return 1;
	}
	if (strcmp(argv[1], "unchanged") == 0) {
		if (argc != 5 || parse_errno(argv[4], &expected_errno) < 0) {
			usage(argv[0]);
			return 2;
		}
		result = verify_unchanged(argv[2], argv[3], expected_errno);
	} else if (strcmp(argv[1], "replace") == 0) {
		if (argc != 4) {
			usage(argv[0]);
			return 2;
		}
		result = verify_replacement(argv[2], argv[3]);
	} else if (strcmp(argv[1], "thread-scope") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			return 2;
		}
		result = verify_thread_scope(argv[2]);
	} else if (strcmp(argv[1], "outcome") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			return 2;
		}
		result = verify_outcome(argv[2]);
	} else if (argc == 3) {
		return run_legacy_trigger(argv[1], argv[2]) < 0 ? 1 : 0;
	} else {
		usage(argv[0]);
		return 2;
	}
	if (result < 0)
		return 1;
	printf("cred_trigger_test: %s verified\n", argv[1]);
	return 0;
}
