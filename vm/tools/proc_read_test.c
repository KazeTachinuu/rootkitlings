// SPDX-License-Identifier: GPL-2.0
// Verify bounded reads and removal behavior for a procfs endpoint.
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MAX_EXPECTED_SIZE (64U * 1024U)

static int load_expected(const char *path, unsigned char **data, size_t *length)
{
	unsigned char buffer[4096];
	unsigned char *contents = NULL;
	size_t capacity = 0;
	size_t used = 0;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "proc_read_test: open expected file %s: %s\n",
			path, strerror(errno));
		return -1;
	}

	for (;;) {
		ssize_t count;

		count = read(fd, buffer, sizeof(buffer));
		if (count < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr,
				"proc_read_test: read expected file %s: %s\n",
				path, strerror(errno));
			close(fd);
			free(contents);
			return -1;
		}
		if (count == 0)
			break;
		if ((size_t)count > MAX_EXPECTED_SIZE - used) {
			fprintf(
			    stderr,
			    "proc_read_test: expected file exceeds %u bytes\n",
			    MAX_EXPECTED_SIZE);
			close(fd);
			free(contents);
			return -1;
		}
		if (used + (size_t)count > capacity) {
			size_t next = capacity == 0 ? sizeof(buffer) : capacity;
			unsigned char *grown;

			while (next < used + (size_t)count)
				next *= 2;
			grown = realloc(contents, next);
			if (grown == NULL) {
				fprintf(stderr,
					"proc_read_test: allocate expected "
					"content: %s\n",
					strerror(errno));
				close(fd);
				free(contents);
				return -1;
			}
			contents = grown;
			capacity = next;
		}
		memcpy(contents + used, buffer, (size_t)count);
		used += (size_t)count;
	}

	if (close(fd) < 0) {
		fprintf(stderr, "proc_read_test: close expected file %s: %s\n",
			path, strerror(errno));
		free(contents);
		return -1;
	}
	*data = contents;
	*length = used;
	return 0;
}

static int verify_reads(int fd, const unsigned char *expected,
			size_t expected_length, const char *phase)
{
	static const size_t first_pattern[] = {1, 3, 2, 7, 4, 11};
	static const size_t second_pattern[] = {5, 2, 9, 1, 13, 3};
	unsigned char buffer[17];
	const size_t *pattern = first_pattern;
	size_t pattern_length =
	    sizeof(first_pattern) / sizeof(first_pattern[0]);
	size_t offset = 0;
	size_t step = 0;
	int pass;

	for (pass = 0; pass < 2; pass++) {
		while (offset < expected_length) {
			size_t requested = pattern[step % pattern_length];
			size_t remaining = expected_length - offset;
			ssize_t count;

			if (remaining <= requested)
				requested = remaining + 1;
			do {
				count = read(fd, buffer, requested);
			} while (count < 0 && errno == EINTR);
			if (count < 0) {
				fprintf(
				    stderr,
				    "proc_read_test: %s read at byte %zu: %s\n",
				    phase, offset, strerror(errno));
				return -1;
			}
			if (count == 0) {
				fprintf(stderr,
					"proc_read_test: %s reached EOF at "
					"byte %zu, expected %zu bytes\n",
					phase, offset, expected_length);
				return -1;
			}
			if ((size_t)count > requested) {
				fprintf(stderr,
					"proc_read_test: %s read returned %zd "
					"bytes for a %zu-byte request\n",
					phase, count, requested);
				return -1;
			}
			if ((size_t)count > remaining) {
				fprintf(stderr,
					"proc_read_test: %s returned content "
					"beyond the expected %zu bytes\n",
					phase, expected_length);
				return -1;
			}
			if (memcmp(buffer, expected + offset, (size_t)count) !=
			    0) {
				fprintf(stderr,
					"proc_read_test: %s content differs at "
					"or after byte %zu\n",
					phase, offset);
				return -1;
			}
			offset += (size_t)count;
			step++;
		}

		for (step = 0; step < 2; step++) {
			ssize_t count;

			do {
				count = read(fd, buffer,
					     step == 0 ? sizeof(buffer) : 1);
			} while (count < 0 && errno == EINTR);
			if (count < 0) {
				fprintf(stderr,
					"proc_read_test: %s EOF read: %s\n",
					phase, strerror(errno));
				return -1;
			}
			if (count != 0) {
				fprintf(stderr,
					"proc_read_test: %s returned %zd extra "
					"byte%s after EOF\n",
					phase, count, count == 1 ? "" : "s");
				return -1;
			}
		}

		if (pass == 0) {
			if (lseek(fd, 0, SEEK_SET) != 0) {
				fprintf(stderr,
					"proc_read_test: %s rewind: %s\n",
					phase, strerror(errno));
				return -1;
			}
			pattern = second_pattern;
			pattern_length =
			    sizeof(second_pattern) / sizeof(second_pattern[0]);
			offset = 0;
			step = 0;
		}
	}

	return 0;
}

static int verify_path(const char *path, const unsigned char *expected,
		       size_t expected_length, const char *phase)
{
	int fd;
	int result;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "proc_read_test: open %s during %s: %s\n", path,
			phase, strerror(errno));
		return -1;
	}
	result = verify_reads(fd, expected, expected_length, phase);
	if (close(fd) < 0) {
		fprintf(stderr, "proc_read_test: close %s during %s: %s\n",
			path, phase, strerror(errno));
		return -1;
	}
	return result;
}

static int verify_write_rejected(const char *path)
{
	static const unsigned char probe = 'x';
	ssize_t count;
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;

	do {
		count = write(fd, &probe, sizeof(probe));
	} while (count < 0 && errno == EINTR);
	if (close(fd) < 0) {
		fprintf(stderr,
			"proc_read_test: close write descriptor for %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	if (count >= 0) {
		fprintf(stderr,
			"proc_read_test: %s accepted a write of %zd byte%s\n",
			path, count, count == 1 ? "" : "s");
		return -1;
	}
	return 0;
}

static int verify_faulting_write(const char *path)
{
	ssize_t count;
	int fd;
	int write_error;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr,
			"proc_read_test: open %s for faulting write: %s\n",
			path, strerror(errno));
		return -1;
	}
	errno = 0;
	count = syscall(SYS_write, fd, (const void *)(uintptr_t)1, 1);
	write_error = errno;
	if (close(fd) < 0) {
		fprintf(stderr,
			"proc_read_test: close faulting write descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	if (count != -1 || write_error != EFAULT) {
		fprintf(stderr,
			"proc_read_test: invalid userspace address returned "
			"%zd (%s), expected EFAULT\n",
			count, strerror(write_error));
		return -1;
	}
	return 0;
}

static int consume_prefix(int fd, const unsigned char *expected,
			  size_t prefix_length)
{
	unsigned char buffer[7];
	size_t offset = 0;

	while (offset < prefix_length) {
		size_t requested = sizeof(buffer);
		ssize_t count;

		if (requested > prefix_length - offset)
			requested = prefix_length - offset;
		do {
			count = read(fd, buffer, requested);
		} while (count < 0 && errno == EINTR);
		if (count < 0) {
			fprintf(
			    stderr,
			    "proc_read_test: held-descriptor prefix read: %s\n",
			    strerror(errno));
			return -1;
		}
		if (count == 0) {
			fprintf(stderr, "proc_read_test: held descriptor "
					"reached EOF before its prefix\n");
			return -1;
		}
		if (memcmp(buffer, expected + offset, (size_t)count) != 0) {
			fprintf(stderr,
				"proc_read_test: held-descriptor prefix "
				"differs at or after byte %zu\n",
				offset);
			return -1;
		}
		offset += (size_t)count;
	}
	return 0;
}

static int verify_unload(const char *path, const char *module,
			 const unsigned char *expected, size_t expected_length)
{
	unsigned char byte;
	size_t prefix_length = expected_length / 2;
	ssize_t count;
	int fd;
	int result = -1;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr,
			"proc_read_test: open held descriptor for %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	if (consume_prefix(fd, expected, prefix_length) < 0)
		goto out;

	if (syscall(SYS_delete_module, module, 0) < 0) {
		fprintf(stderr, "proc_read_test: delete module %s: %s\n",
			module, strerror(errno));
		goto out;
	}

	do {
		count = read(fd, &byte, sizeof(byte));
	} while (count < 0 && errno == EINTR);
	if (count > 0) {
		fprintf(stderr,
			"proc_read_test: stale descriptor returned %zd byte%s "
			"after unloading %s\n",
			count, count == 1 ? "" : "s", module);
		goto out;
	}
	result = 0;

out:
	if (close(fd) < 0) {
		fprintf(stderr,
			"proc_read_test: close held descriptor for %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	return result;
}

int main(int argc, char **argv)
{
	unsigned char *expected;
	size_t expected_length;
	const char *module = NULL;
	const char *path;
	int test_faulting_write = 0;
	int result = 1;

	if (argc < 3 || argc > 5) {
		fprintf(stderr,
			"usage: %s PATH EXPECTED_FILE [MODULE [fault-write]]\n",
			argv[0]);
		return 2;
	}
	path = argv[1];
	if (argc >= 4)
		module = argv[3];
	if (argc == 5) {
		if (strcmp(argv[4], "fault-write") != 0) {
			fprintf(stderr, "proc_read_test: unknown mode %s\n",
				argv[4]);
			return 2;
		}
		test_faulting_write = 1;
	}

	if (load_expected(argv[2], &expected, &expected_length) < 0)
		return 2;
	if (module != NULL && expected_length < 2) {
		fprintf(stderr, "proc_read_test: unload verification needs at "
				"least two expected bytes\n");
		free(expected);
		return 2;
	}

	if (verify_path(path, expected, expected_length, "initial read") < 0)
		goto out;
	if (test_faulting_write && verify_faulting_write(path) < 0)
		goto out;
	if (verify_write_rejected(path) < 0)
		goto out;
	if (verify_path(path, expected, expected_length, "post-write read") < 0)
		goto out;
	if (module != NULL &&
	    verify_unload(path, module, expected, expected_length) < 0)
		goto out;

	printf("proc_read_test: verified %s%s\n", path,
	       module == NULL ? "" : " through module unload");
	result = 0;

out:
	free(expected);
	return result;
}
