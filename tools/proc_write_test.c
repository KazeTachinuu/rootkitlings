// SPDX-License-Identifier: GPL-2.0
// Verify write behavior for a procfs endpoint.
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MAX_FIXTURE_SIZE (64U * 1024U)
#define MAX_REPEAT_COUNT 1024

struct fixture {
	unsigned char *data;
	size_t length;
};

static void usage(const char *program)
{
	fprintf(stderr,
		"usage:\n"
		"  %s write PATH DATA_FILE\n"
		"  %s outcome PATH DATA_FILE\n"
		"  %s repeat PATH DATA_FILE COUNT\n"
		"  %s errno PATH DATA_FILE ERRNO\n"
		"  %s fault PATH DATA_FILE ERRNO\n"
		"  %s offset PATH DATA_FILE OFFSET ERRNO\n"
		"  %s unload PATH DATA_FILE MODULE\n",
		program, program, program, program, program, program, program);
}

static int parse_positive(const char *text, long maximum, const char *name,
			  long *value)
{
	char *end = NULL;
	long parsed;

	errno = 0;
	parsed = strtol(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed <= 0 ||
	    parsed > maximum) {
		fprintf(stderr, "proc_write_test: invalid %s: %s\n", name, text);
		return -1;
	}
	*value = parsed;
	return 0;
}

static int load_fixture(const char *path, struct fixture *fixture)
{
	struct stat status;
	unsigned char *data;
	size_t offset = 0;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "proc_write_test: open data file %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	if (fstat(fd, &status) < 0) {
		fprintf(stderr, "proc_write_test: inspect data file %s: %s\n",
			path, strerror(errno));
		close(fd);
		return -1;
	}
	if (status.st_size < 0 || (uintmax_t)status.st_size > MAX_FIXTURE_SIZE) {
		fprintf(stderr,
			"proc_write_test: data file exceeds %u bytes: %s\n",
			MAX_FIXTURE_SIZE, path);
		close(fd);
		return -1;
	}

	fixture->length = (size_t)status.st_size;
	data = fixture->length == 0 ? NULL : malloc(fixture->length);
	if (fixture->length != 0 && data == NULL) {
		fprintf(stderr, "proc_write_test: allocate data buffer: %s\n",
			strerror(errno));
		close(fd);
		return -1;
	}
	while (offset < fixture->length) {
		ssize_t count;

		count = read(fd, data + offset, fixture->length - offset);
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0) {
			fprintf(stderr, "proc_write_test: read data file %s: %s\n",
				path, strerror(errno));
			free(data);
			close(fd);
			return -1;
		}
		if (count == 0) {
			fprintf(stderr,
				"proc_write_test: data file changed while reading: %s\n",
				path);
			free(data);
			close(fd);
			return -1;
		}
		offset += (size_t)count;
	}
	if (close(fd) < 0) {
		fprintf(stderr, "proc_write_test: close data file %s: %s\n",
			path, strerror(errno));
		free(data);
		return -1;
	}
	fixture->data = data;
	return 0;
}

static int open_endpoint(const char *path)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);

	if (fd < 0)
		fprintf(stderr, "proc_write_test: open %s: %s\n", path,
			strerror(errno));
	return fd;
}

static ssize_t write_retry(int fd, const void *data, size_t length)
{
	ssize_t count;

	do {
		count = write(fd, data, length);
	} while (count < 0 && errno == EINTR);
	return count;
}

static int expect_full_write(const char *path, const struct fixture *fixture)
{
	ssize_t count;
	int fd = open_endpoint(path);
	int result = -1;

	if (fd < 0)
		return -1;
	count = write_retry(fd, fixture->data, fixture->length);
	if (count < 0) {
		fprintf(stderr, "proc_write_test: write %s: %s\n", path,
			strerror(errno));
	} else if ((size_t)count != fixture->length) {
		fprintf(stderr,
			"proc_write_test: write %s returned %zd, expected %zu\n",
			path, count, fixture->length);
	} else {
		result = 0;
	}
	if (close(fd) < 0) {
		fprintf(stderr, "proc_write_test: close %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	return result;
}

static int report_write_outcome(const char *path,
				const struct fixture *fixture)
{
	ssize_t count;
	int saved_errno;
	int fd = open_endpoint(path);

	if (fd < 0)
		return -1;
	errno = 0;
	count = write_retry(fd, fixture->data, fixture->length);
	saved_errno = errno;
	if (close(fd) < 0) {
		fprintf(stderr, "proc_write_test: close %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	if (count < 0) {
		printf("%d\n", saved_errno);
		return 0;
	}
	if ((size_t)count != fixture->length) {
		fprintf(stderr,
			"proc_write_test: write %s returned %zd, expected %zu\n",
			path, count, fixture->length);
		return -1;
	}
	printf("ok\n");
	return 0;
}

static int expect_repeated_writes(const char *path,
				  const struct fixture *fixture, long repetitions)
{
	long iteration;
	int fd = open_endpoint(path);
	int result = -1;

	if (fd < 0)
		return -1;
	for (iteration = 1; iteration <= repetitions; iteration++) {
		ssize_t count = write_retry(fd, fixture->data, fixture->length);

		if (count < 0) {
			fprintf(stderr,
				"proc_write_test: write %ld/%ld to %s: %s\n",
				iteration, repetitions, path, strerror(errno));
			goto out;
		}
		if ((size_t)count != fixture->length) {
			fprintf(stderr,
				"proc_write_test: write %ld/%ld to %s returned %zd, expected %zu\n",
				iteration, repetitions, path, count, fixture->length);
			goto out;
		}
	}
	result = 0;

out:
	if (close(fd) < 0) {
		fprintf(stderr, "proc_write_test: close %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	return result;
}

static int expect_errno(const char *path, const void *data, size_t length,
			int expected_errno)
{
	ssize_t count;
	int saved_errno;
	int fd = open_endpoint(path);

	if (fd < 0)
		return -1;
	errno = 0;
	count = write_retry(fd, data, length);
	saved_errno = errno;
	if (close(fd) < 0) {
		fprintf(stderr, "proc_write_test: close %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	if (count >= 0) {
		fprintf(stderr,
			"proc_write_test: write %s returned %zd, expected errno=%d\n",
			path, count, expected_errno);
		return -1;
	}
	if (saved_errno != expected_errno) {
		fprintf(stderr,
			"proc_write_test: write %s returned errno=%d, expected errno=%d\n",
			path, saved_errno, expected_errno);
		return -1;
	}
	return 0;
}

static int expect_fault(const char *path, const struct fixture *fixture,
			int expected_errno)
{
	unsigned char *mapping;
	unsigned char *source;
	size_t mapping_length;
	size_t prefix;
	long page_size = sysconf(_SC_PAGESIZE);
	int result;

	if (fixture->length < 2) {
		fprintf(stderr,
			"proc_write_test: fault data file must contain at least 2 bytes\n");
		return -1;
	}
	if (page_size <= 0 || page_size > LONG_MAX / 2) {
		fprintf(stderr, "proc_write_test: invalid page size: %ld\n",
			page_size);
		return -1;
	}
	mapping_length = (size_t)page_size * 2;
	mapping = mmap(NULL, mapping_length, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "proc_write_test: map guard buffer: %s\n",
			strerror(errno));
		return -1;
	}
	prefix = fixture->length / 2;
	if (prefix > (size_t)page_size)
		prefix = (size_t)page_size;
	source = mapping + (size_t)page_size - prefix;
	memcpy(source, fixture->data, prefix);
	if (mprotect(mapping + page_size, (size_t)page_size, PROT_NONE) < 0) {
		fprintf(stderr, "proc_write_test: protect guard page: %s\n",
			strerror(errno));
		munmap(mapping, mapping_length);
		return -1;
	}
	result = expect_errno(path, source, fixture->length, expected_errno);
	if (munmap(mapping, mapping_length) < 0) {
		fprintf(stderr, "proc_write_test: unmap guard buffer: %s\n",
			strerror(errno));
		return -1;
	}
	return result;
}

static int expect_offset_errno(const char *path, const struct fixture *fixture,
			       off_t offset, int expected_errno)
{
	ssize_t count;
	int saved_errno;
	int fd = open_endpoint(path);
	off_t positioned;

	if (fd < 0)
		return -1;
	do {
		positioned = lseek(fd, offset, SEEK_SET);
	} while (positioned < 0 && errno == EINTR);
	if (positioned < 0) {
		fprintf(stderr, "proc_write_test: seek %s to offset %jd: %s\n",
			path, (intmax_t)offset, strerror(errno));
		close(fd);
		return -1;
	}
	if (positioned != offset) {
		fprintf(stderr,
			"proc_write_test: seek %s returned offset %jd, expected %jd\n",
			path, (intmax_t)positioned, (intmax_t)offset);
		close(fd);
		return -1;
	}
	errno = 0;
	count = write_retry(fd, fixture->data, fixture->length);
	saved_errno = errno;
	if (close(fd) < 0) {
		fprintf(stderr, "proc_write_test: close %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	if (count >= 0) {
		fprintf(stderr,
			"proc_write_test: write %s at offset %jd returned %zd, expected errno=%d\n",
			path, (intmax_t)offset, count, expected_errno);
		return -1;
	}
	if (saved_errno != expected_errno) {
		fprintf(stderr,
			"proc_write_test: write %s at offset %jd returned errno=%d, expected errno=%d\n",
			path, (intmax_t)offset, saved_errno, expected_errno);
		return -1;
	}
	return 0;
}

static int expect_unload_rejection(const char *path,
				   const struct fixture *fixture,
				   const char *module)
{
	ssize_t count;
	int fd = open_endpoint(path);

	if (fd < 0)
		return -1;
	while (syscall(SYS_delete_module, module, 0) < 0) {
		if (errno == EINTR)
			continue;
		fprintf(stderr, "proc_write_test: delete module %s: %s\n",
			module, strerror(errno));
		close(fd);
		return -1;
	}
	count = write_retry(fd, fixture->data, fixture->length);
	if (close(fd) < 0) {
		fprintf(stderr, "proc_write_test: close stale descriptor for %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	if (count > 0) {
		fprintf(stderr,
			"proc_write_test: stale descriptor accepted %zd byte%s after unloading %s\n",
			count, count == 1 ? "" : "s", module);
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct fixture fixture = { 0 };
	const char *mode;
	const char *path;
	long expected_errno;
	long offset;
	long repetitions;
	int result = -1;

	if (argc < 4) {
		usage(argv[0]);
		return 2;
	}
	mode = argv[1];
	path = argv[2];
	if (load_fixture(argv[3], &fixture) < 0)
		return 2;
	if (fixture.length == 0 && strcmp(mode, "errno") != 0) {
		fprintf(stderr,
			"proc_write_test: %s mode needs a nonempty data file\n",
			mode);
		goto invalid;
	}

	if (strcmp(mode, "write") == 0 && argc == 4) {
		result = expect_full_write(path, &fixture);
	} else if (strcmp(mode, "outcome") == 0 && argc == 4) {
		result = report_write_outcome(path, &fixture);
	} else if (strcmp(mode, "repeat") == 0 && argc == 5) {
		if (parse_positive(argv[4], MAX_REPEAT_COUNT, "count",
				   &repetitions) < 0)
			goto invalid;
		result = expect_repeated_writes(path, &fixture, repetitions);
	} else if (strcmp(mode, "errno") == 0 && argc == 5) {
		if (parse_positive(argv[4], 4095, "errno", &expected_errno) < 0)
			goto invalid;
		result = expect_errno(path, fixture.data, fixture.length,
				      (int)expected_errno);
	} else if (strcmp(mode, "fault") == 0 && argc == 5) {
		if (parse_positive(argv[4], 4095, "errno", &expected_errno) < 0)
			goto invalid;
		result = expect_fault(path, &fixture, (int)expected_errno);
	} else if (strcmp(mode, "offset") == 0 && argc == 6) {
		if (parse_positive(argv[4], LONG_MAX, "offset", &offset) < 0 ||
		    parse_positive(argv[5], 4095, "errno", &expected_errno) < 0)
			goto invalid;
		result = expect_offset_errno(path, &fixture, (off_t)offset,
					     (int)expected_errno);
	} else if (strcmp(mode, "unload") == 0 && argc == 5) {
		result = expect_unload_rejection(path, &fixture, argv[4]);
	} else {
		usage(argv[0]);
		goto invalid;
	}

	free(fixture.data);
	if (result < 0)
		return 1;
	if (strcmp(mode, "outcome") == 0)
		return 0;
	printf("proc_write_test: verified %s mode for %s\n", mode, path);
	return 0;

invalid:
	free(fixture.data);
	return 2;
}
