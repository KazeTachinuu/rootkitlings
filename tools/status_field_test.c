// SPDX-License-Identifier: GPL-2.0
// Read and observe unsigned counters in line-oriented key=value status files.
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_STATUS_SIZE (64U * 1024U)
#define MAX_WAIT_MS 600000U
#define POLL_MS 10U

static int parse_unsigned(const char *text, uint64_t limit, const char *name,
			  uint64_t *value)
{
	char *end;
	const unsigned char *cursor;
	uintmax_t parsed;

	if (text[0] == '\0')
		goto invalid;
	for (cursor = (const unsigned char *)text; *cursor != '\0'; cursor++)
		if (*cursor < '0' || *cursor > '9')
			goto invalid;
	errno = 0;
	parsed = strtoumax(text, &end, 10);
	if (errno == ERANGE || *end != '\0' || parsed > limit)
		goto invalid;
	*value = (uint64_t)parsed;
	return 0;

invalid:
	fprintf(stderr, "status_field_test: invalid %s: %s\n", name, text);
	return -1;
}

static int valid_field(const char *field)
{
	size_t index;

	if (field[0] == '\0')
		return 0;
	for (index = 0; field[index] != '\0'; index++) {
		unsigned char byte = (unsigned char)field[index];

		if ((byte >= 'a' && byte <= 'z') ||
		    (byte >= 'A' && byte <= 'Z') ||
		    (byte >= '0' && byte <= '9') || byte == '_' || byte == '-')
			continue;
		return 0;
	}
	return 1;
}

static int read_status(const char *path, char *buffer, size_t capacity,
			       size_t *length)
{
	size_t used = 0;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "status_field_test: open %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	for (;;) {
		ssize_t count;

		do {
			count = read(fd, buffer + used, capacity - used);
		} while (count < 0 && errno == EINTR);
		if (count < 0) {
			fprintf(stderr, "status_field_test: read %s: %s\n", path,
				strerror(errno));
			close(fd);
			return -1;
		}
		if (count == 0)
			break;
		used += (size_t)count;
		if (used == capacity) {
			unsigned char extra;

			do {
				count = read(fd, &extra, sizeof(extra));
			} while (count < 0 && errno == EINTR);
			if (count < 0) {
				fprintf(stderr, "status_field_test: read %s: %s\n",
					path, strerror(errno));
				close(fd);
				return -1;
			}
			if (count != 0) {
				fprintf(stderr,
					"status_field_test: %s exceeds %u bytes\n",
					path, MAX_STATUS_SIZE);
				close(fd);
				return -1;
			}
			break;
		}
	}
	if (close(fd) < 0) {
		fprintf(stderr, "status_field_test: close %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	if (memchr(buffer, '\0', used) != NULL) {
		fprintf(stderr, "status_field_test: %s contains a NUL byte\n", path);
		return -1;
	}
	buffer[used] = '\0';
	*length = used;
	return 0;
}

static int parse_field(char *buffer, size_t length, const char *field,
		       uint64_t *value)
{
	char *cursor = buffer;
	char *end = buffer + length;
	size_t field_length = strlen(field);
	int found = 0;

	while (cursor < end) {
		char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
		char *value_text;

		if (line_end == NULL)
			line_end = end;
		if ((size_t)(line_end - cursor) > field_length &&
		    memcmp(cursor, field, field_length) == 0 &&
		    cursor[field_length] == '=') {
			if (found) {
				fprintf(stderr,
					"status_field_test: duplicate field: %s\n",
					field);
				return -1;
			}
			*line_end = '\0';
			value_text = cursor + field_length + 1;
			if (parse_unsigned(value_text, UINT64_MAX, field, value) < 0)
				return -1;
			found = 1;
		}
		cursor = line_end == end ? end : line_end + 1;
	}
	if (!found) {
		fprintf(stderr, "status_field_test: field not found: %s\n", field);
		return -1;
	}
	return 0;
}

static int read_field(const char *path, const char *field, uint64_t *value)
{
	char buffer[MAX_STATUS_SIZE + 1];
	size_t length;

	if (read_status(path, buffer, MAX_STATUS_SIZE, &length) < 0)
		return -1;
	return parse_field(buffer, length, field, value);
}

static int monotonic_ms(uint64_t *milliseconds)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
		fprintf(stderr, "status_field_test: read monotonic clock: %s\n",
			strerror(errno));
		return -1;
	}
	*milliseconds = (uint64_t)now.tv_sec * 1000U +
		(uint64_t)now.tv_nsec / 1000000U;
	return 0;
}

static int pause_poll(void)
{
	struct timespec interval = {
		.tv_sec = 0,
		.tv_nsec = POLL_MS * 1000000U,
	};

	while (nanosleep(&interval, &interval) < 0) {
		if (errno == EINTR)
			continue;
		fprintf(stderr, "status_field_test: wait between samples: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static int wait_for_advance(const char *path, const char *field,
			    uint64_t baseline, uint64_t timeout_ms)
{
	uint64_t deadline;

	if (monotonic_ms(&deadline) < 0)
		return -1;
	deadline += timeout_ms;
	for (;;) {
		uint64_t now;
		uint64_t value;

		if (read_field(path, field, &value) < 0)
			return -1;
		if (value > baseline) {
			printf("%" PRIu64 "\n", value);
			return 0;
		}
		if (monotonic_ms(&now) < 0)
			return -1;
		if (now >= deadline) {
			fprintf(stderr,
				"status_field_test: %s stayed at %" PRIu64
				" for %" PRIu64 " ms\n",
				field, value, timeout_ms);
			return -1;
		}
		if (pause_poll() < 0)
			return -1;
	}
}

static int verify_unchanged(const char *path, const char *field,
			    uint64_t expected, uint64_t duration_ms)
{
	uint64_t deadline;

	if (monotonic_ms(&deadline) < 0)
		return -1;
	deadline += duration_ms;
	for (;;) {
		uint64_t now;
		uint64_t value;

		if (read_field(path, field, &value) < 0)
			return -1;
		if (value != expected) {
			fprintf(stderr,
				"status_field_test: %s changed from %" PRIu64
				" to %" PRIu64 "\n",
				field, expected, value);
			return -1;
		}
		if (monotonic_ms(&now) < 0)
			return -1;
		if (now >= deadline) {
			printf("%" PRIu64 "\n", value);
			return 0;
		}
		if (pause_poll() < 0)
			return -1;
	}
}

static void usage(const char *program)
{
	fprintf(stderr,
		"usage: %s read PATH FIELD\n"
		"       %s advance PATH FIELD BASELINE TIMEOUT_MS\n"
		"       %s unchanged PATH FIELD VALUE DURATION_MS\n",
		program, program, program);
}

int main(int argc, char **argv)
{
	uint64_t first;
	uint64_t second;

	if (argc < 4 || !valid_field(argv[3])) {
		usage(argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "read") == 0 && argc == 4) {
		if (read_field(argv[2], argv[3], &first) < 0)
			return 1;
		printf("%" PRIu64 "\n", first);
		return 0;
	}
	if (strcmp(argv[1], "advance") == 0 && argc == 6) {
		if (parse_unsigned(argv[4], UINT64_MAX, "baseline", &first) < 0 ||
		    parse_unsigned(argv[5], MAX_WAIT_MS, "timeout", &second) < 0)
			return 2;
		return wait_for_advance(argv[2], argv[3], first, second) < 0;
	}
	if (strcmp(argv[1], "unchanged") == 0 && argc == 6) {
		if (parse_unsigned(argv[4], UINT64_MAX, "value", &first) < 0 ||
		    parse_unsigned(argv[5], MAX_WAIT_MS, "duration", &second) < 0)
			return 2;
		return verify_unchanged(argv[2], argv[3], first, second) < 0;
	}
	usage(argv[0]);
	return 2;
}
