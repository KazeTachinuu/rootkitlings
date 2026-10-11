#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef RK_STANDALONE_HELPER
size_t rk_remove_lines(unsigned char *buffer, size_t length,
		       const unsigned char *target, size_t target_length);

static void fail(const char *label)
{
	fprintf(stderr, "[x] %s\n", label);
	exit(1);
}

static size_t reference(unsigned char *buffer, size_t length,
			const unsigned char *target, size_t target_length)
{
	size_t read_pos = 0;
	size_t write_pos = 0;

	if (target_length == 0 || target_length > length)
		return length;
	while (read_pos < length) {
		const unsigned char *line = buffer + read_pos;
		const unsigned char *nl = memchr(line, '\n',
						 length - read_pos);
		size_t line_len = nl ? (size_t)(nl - line + 1)
				     : (length - read_pos);
		int match = 0;

		if (line_len >= target_length) {
			size_t end = line_len - target_length;
			size_t i;

			for (i = 0; i <= end; i++) {
				if (memcmp(line + i, target,
					   target_length) == 0) {
					match = 1;
					break;
				}
			}
		}
		if (!match) {
			if (write_pos != read_pos)
				memmove(buffer + write_pos, line, line_len);
			write_pos += line_len;
		}
		read_pos += line_len;
	}
	return write_pos;
}

static void check(const char *label, const unsigned char *input, size_t length,
		  const unsigned char *target, size_t target_length)
{
	unsigned char actual[514];
	unsigned char expected[514];
	size_t got;
	size_t want;

	if (length > 512)
		fail("test fixture exceeds bounded result");
	memset(actual, 0xa5, sizeof(actual));
	memset(expected, 0xa5, sizeof(expected));
	memcpy(actual + 1, input, length);
	memcpy(expected + 1, input, length);
	want = reference(expected + 1, length, target, target_length);
	got = rk_remove_lines(actual + 1, length, target, target_length);
	if (got != want || memcmp(actual, expected, sizeof(actual)) != 0)
		fail(label);
}

static uint32_t random_state = 0x7a2d91e3U;

static unsigned char next_byte(void)
{
	random_state = random_state * 1664525U + 1013904223U;
	return (unsigned char)(random_state >> 24);
}

static void generated_cases(void)
{
	unsigned char input[512];
	unsigned char target[8];
	size_t trial;

	for (trial = 0; trial < 4096; trial++) {
		size_t index;
		size_t length = next_byte() * 2U;
		size_t tlen = (next_byte() % 7U) + 1U;

		for (index = 0; index < sizeof(input); index++)
			input[index] = next_byte();
		for (index = 0; index < sizeof(target); index++)
			target[index] = next_byte();
		if (tlen <= length && (trial % 3U) == 0) {
			size_t at = next_byte() % (length - tlen + 1U);
			memcpy(input + at, target, tlen);
		}
		if (trial % 5U == 0) {
			size_t nl_count = (next_byte() % 4U) + 1U;
			for (index = 0; index < nl_count && index < length; index++) {
				size_t pos = next_byte() % length;
				input[pos] = '\n';
			}
		}
		check("generated reference corpus", input, length,
		      target, tlen);
	}
}

static void guard_page_case(void)
{
	const long page_size = sysconf(_SC_PAGESIZE);
	unsigned char *mapping;
	unsigned char *bytes;
	const unsigned char target[] = "SECRET";

	if (page_size <= 0)
		fail("page size");
	mapping = mmap(NULL, (size_t)page_size * 3U, PROT_NONE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		fail("guard-page mmap");
	if (mprotect(mapping + page_size, (size_t)page_size,
		     PROT_READ | PROT_WRITE) != 0)
		fail("guard-page mprotect");
	bytes = mapping + page_size * 2U - 32U;
	memcpy(bytes, "safe line\nSECRET hidden\nvisible\n", 32);
	size_t result = rk_remove_lines(bytes, 32, target, 6);
	if (result != 18)
		fail("guard-page line removal");
	if (memcmp(bytes, "safe line\nvisible\n", 18) != 0)
		fail("guard-page content");
	if (munmap(mapping, (size_t)page_size * 3U) != 0)
		fail("guard-page munmap");
}

int main(void)
{
	static const unsigned char target[] = "SECRET";
	check("empty input", (const unsigned char *)"", 0, target, 6);
	check("no match", (const unsigned char *)"safe line\n", 10, target, 6);
	check("single match",
	      (const unsigned char *)"SECRET line\n", 12, target, 6);
	check("match without newline",
	      (const unsigned char *)"has SECRET", 10, target, 6);
	check("middle line match",
	      (const unsigned char *)"keep\nSECRET drop\nkeep2\n", 22,
	      target, 6);
	check("multiple matches",
	      (const unsigned char *)"SECRET a\nSECRET b\n", 18, target, 6);
	check("all lines match",
	      (const unsigned char *)"SECRET\nSECRET\n", 14, target, 6);
	check("no lines match",
	      (const unsigned char *)"safe\nlines\nonly\n", 15, target, 6);
	check("target at end of line",
	      (const unsigned char *)"endSECRET\nkeep\n", 15, target, 6);
	check("target at start of line",
	      (const unsigned char *)"SECRETwo\nkeep\n", 14, target, 6);
	check("embedded in larger word",
	      (const unsigned char *)"xSECRETx\nkeep\n", 14, target, 6);
	check("last line no newline",
	      (const unsigned char *)"keep\nSECRET", 11, target, 6);
	check("target longer than input",
	      (const unsigned char *)"ab\n", 3, target, 6);
	check("zero-length target",
	      (const unsigned char *)"keep\n", 5,
	      (const unsigned char *)"", 0);
	generated_cases();
	guard_page_case();
	puts("[+] bounded line removal verified");
	return 0;
}
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>

static void usage(const char *name)
{
	fprintf(stderr,
		"usage: %s write PATH HEX | invalid PATH | "
		"error METHOD PATH ERRNO | "
		"position-error PATH ERRNO HEX | "
		"check METHOD PATH OFFSET REQUEST HEX | "
		"contains|excludes METHOD PATH TEXT | "
		"proc PATH HEX | stress PATH ITERATIONS HEX [ALTERNATE_HEX]\n",
		name);
}

static int hex_value(char byte)
{
	if (byte >= '0' && byte <= '9')
		return byte - '0';
	if (byte >= 'a' && byte <= 'f')
		return byte - 'a' + 10;
	if (byte >= 'A' && byte <= 'F')
		return byte - 'A' + 10;
	return -1;
}

static unsigned char *decode_hex(const char *text, size_t *length)
{
	const size_t digits = strlen(text);
	unsigned char *bytes;
	size_t index;

	if ((digits & 1U) != 0)
		return NULL;
	bytes = malloc(digits / 2U + 1U);
	if (!bytes)
		return NULL;
	for (index = 0; index < digits; index += 2U) {
		int high = hex_value(text[index]);
		int low = hex_value(text[index + 1U]);

		if (high < 0 || low < 0) {
			free(bytes);
			return NULL;
		}
		bytes[index / 2U] = (unsigned char)((high << 4) | low);
	}
	*length = digits / 2U;
	return bytes;
}

static int write_hex(const char *path, const char *hex)
{
	unsigned char *bytes;
	size_t length;
	ssize_t result;
	int descriptor;

	bytes = decode_hex(hex, &length);
	if (!bytes)
		return 2;
	descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (descriptor < 0) {
		free(bytes);
		return 2;
	}
	result = write(descriptor, bytes, length);
	free(bytes);
	if (close(descriptor) != 0 || result != (ssize_t)length)
		return 2;
	return 0;
}

static ssize_t read_by_method(const char *method, int descriptor,
			      unsigned char *buffer, size_t request,
			      off_t offset)
{
	if (strcmp(method, "read") == 0) {
		if (lseek(descriptor, offset, SEEK_SET) != offset)
			return -1;
		return syscall(SYS_read, descriptor, buffer, request);
	}
	if (strcmp(method, "pread64") == 0)
		return syscall(SYS_pread64, descriptor, buffer, request, offset);
	if (strcmp(method, "readv") == 0 || strcmp(method, "preadv") == 0) {
		struct iovec vectors[2];
		size_t first = request / 2U;

		vectors[0].iov_base = buffer;
		vectors[0].iov_len = first;
		vectors[1].iov_base = buffer + first;
		vectors[1].iov_len = request - first;
		if (strcmp(method, "readv") == 0) {
			if (lseek(descriptor, offset, SEEK_SET) != offset)
				return -1;
			return syscall(SYS_readv, descriptor, vectors, 2);
		}
		return syscall(SYS_preadv, descriptor, vectors, 2, offset, 0);
	}
	if (strcmp(method, "mmap") == 0) {
		struct stat status;
		unsigned char *mapping;
		size_t available;

		if (fstat(descriptor, &status) != 0 || offset < 0 ||
		    (uintmax_t)offset >= (uintmax_t)status.st_size) {
			errno = EINVAL;
			return -1;
		}
		available = (size_t)(status.st_size - offset);
		if (request > available)
			request = available;
		mapping = mmap(NULL, (size_t)status.st_size, PROT_READ,
			       MAP_PRIVATE, descriptor, 0);
		if (mapping == MAP_FAILED)
			return -1;
		memcpy(buffer, mapping + offset, request);
		if (munmap(mapping, (size_t)status.st_size) != 0)
			return -1;
		return (ssize_t)request;
	}
	if (strcmp(method, "sendfile") == 0) {
		int pipe_descriptors[2];
		off_t position = offset;
		ssize_t sent;
		ssize_t received;

		if (pipe(pipe_descriptors) != 0)
			return -1;
		sent = sendfile(pipe_descriptors[1], descriptor, &position, request);
		if (close(pipe_descriptors[1]) != 0 || sent < 0) {
			close(pipe_descriptors[0]);
			return -1;
		}
		received = read(pipe_descriptors[0], buffer, (size_t)sent);
		close(pipe_descriptors[0]);
		return received == sent ? received : -1;
	}
	errno = EINVAL;
	return -1;
}

static int check_read(const char *method, const char *path,
		      const char *offset_text, const char *request_text,
		      const char *hex)
{
	unsigned char *expected;
	unsigned char *actual;
	char *end;
	unsigned long long parsed_offset;
	unsigned long parsed_request;
	size_t expected_length;
	ssize_t result;
	int descriptor;

	errno = 0;
	parsed_offset = strtoull(offset_text, &end, 10);
	if (errno || *end != '\0' || parsed_offset > INT64_MAX)
		return 2;
	errno = 0;
	parsed_request = strtoul(request_text, &end, 10);
	if (errno || *end != '\0' || parsed_request > 65536U)
		return 2;
	expected = decode_hex(hex, &expected_length);
	actual = calloc(parsed_request + 1U, 1U);
	if (!expected || !actual) {
		free(expected);
		free(actual);
		return 2;
	}
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		free(expected);
		free(actual);
		return 2;
	}
	result = read_by_method(method, descriptor, actual, parsed_request,
				(off_t)parsed_offset);
	close(descriptor);
	if (result != (ssize_t)expected_length ||
	    memcmp(actual, expected, expected_length) != 0) {
		fprintf(stderr, "[x] %s returned %zd bytes, expected %zu\n",
			method, result, expected_length);
		free(expected);
		free(actual);
		return 1;
	}
	free(expected);
	free(actual);
	return 0;
}

static bool contains_bytes(const unsigned char *bytes, size_t length,
			   const char *text)
{
	const size_t text_length = strlen(text);
	size_t index;

	if (!text_length)
		return true;
	if (text_length > length)
		return false;
	for (index = 0; index <= length - text_length; index++) {
		if (memcmp(bytes + index, text, text_length) == 0)
			return true;
	}
	return false;
}

static int check_contains(const char *method, const char *path,
			  const char *text, bool expected)
{
	unsigned char buffer[4096];
	ssize_t result;
	int descriptor;

	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return 2;
	result = read_by_method(method, descriptor, buffer, sizeof(buffer), 0);
	close(descriptor);
	if (result < 0)
		return 2;
	return contains_bytes(buffer, (size_t)result, text) == expected ? 0 : 1;
}

static int invalid_read(const char *path)
{
	int descriptor = open(path, O_RDONLY | O_CLOEXEC);
	long result;

	if (descriptor < 0)
		return 2;
	errno = 0;
	result = syscall(SYS_read, descriptor, (void *)(uintptr_t)1, 32);
	close(descriptor);
	return result == -1 && errno == EFAULT ? 0 : 1;
}

static int expect_error(const char *method, const char *path,
			const char *errno_text)
{
	unsigned char buffer[64];
	char *end;
	long expected;
	ssize_t result;
	int descriptor;

	errno = 0;
	expected = strtol(errno_text, &end, 10);
	if (errno || *end != '\0' || expected <= 0 || expected > 4095)
		return 2;
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return 2;
	errno = 0;
	result = read_by_method(method, descriptor, buffer, sizeof(buffer), 0);
	close(descriptor);
	return result == -1 && errno == expected ? 0 : 1;
}

static int expect_position_error(const char *path, const char *errno_text,
				 const char *hex)
{
	struct iovec vector;
	unsigned char failed_buffer[64];
	unsigned char *expected;
	unsigned char *actual;
	char *end;
	long expected_errno;
	size_t length;
	ssize_t result;
	int descriptor;

	errno = 0;
	expected_errno = strtol(errno_text, &end, 10);
	if (errno || *end != '\0' || expected_errno <= 0 ||
	    expected_errno > 4095)
		return 2;
	expected = decode_hex(hex, &length);
	actual = calloc(length + 1U, 1U);
	if (!expected || !actual || length == 0 ||
	    length > sizeof(failed_buffer)) {
		free(expected);
		free(actual);
		return 2;
	}
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		free(expected);
		free(actual);
		return 2;
	}
	errno = 0;
	result = syscall(SYS_read, descriptor, failed_buffer, length);
	if (result != -1 || errno != expected_errno) {
		close(descriptor);
		free(expected);
		free(actual);
		return 1;
	}
	vector.iov_base = actual;
	vector.iov_len = length;
	result = syscall(SYS_readv, descriptor, &vector, 1);
	close(descriptor);
	if (result != (ssize_t)length ||
	    memcmp(actual, expected, length) != 0) {
		free(expected);
		free(actual);
		return 1;
	}
	free(expected);
	free(actual);
	return 0;
}

struct stress_args {
	const char *path;
	const unsigned char *expected;
	const unsigned char *alternate;
	size_t length;
	size_t alternate_length;
	unsigned long iterations;
	int failed;
};

static void *stress_thread(void *opaque)
{
	struct stress_args *args = opaque;
	unsigned char buffer[4096];
	unsigned long iteration;
	int descriptor = open(args->path, O_RDONLY | O_CLOEXEC);

	if (descriptor < 0) {
		args->failed = 1;
		return NULL;
	}
	for (iteration = 0; iteration < args->iterations; iteration++) {
		ssize_t result = syscall(SYS_pread64, descriptor, buffer,
					 sizeof(buffer), 0);

		if (result == (ssize_t)args->length &&
		    memcmp(buffer, args->expected, args->length) == 0)
			continue;
		if (args->alternate &&
		    result == (ssize_t)args->alternate_length &&
		    memcmp(buffer, args->alternate, args->alternate_length) == 0)
			continue;
		args->failed = 1;
		break;
	}
	close(descriptor);
	return NULL;
}

static int stress(const char *path, const char *iterations_text,
		  const char *hex, const char *alternate_hex)
{
	struct stress_args args[12];
	pthread_t threads[12];
	unsigned char *expected;
	unsigned char *alternate = NULL;
	char *end;
	unsigned long iterations;
	size_t length;
	size_t alt_length = 0;
	size_t index;

	errno = 0;
	iterations = strtoul(iterations_text, &end, 10);
	if (errno || *end != '\0' || iterations == 0 || iterations > 100000)
		return 2;
	expected = decode_hex(hex, &length);
	if (!expected || length > 4096) {
		free(expected);
		return 2;
	}
	if (alternate_hex) {
		alternate = decode_hex(alternate_hex, &alt_length);
		if (!alternate || alt_length > 4096) {
			free(expected);
			free(alternate);
			return 2;
		}
	}
	for (index = 0; index < 12; index++) {
		args[index] = (struct stress_args){
			.path = path,
			.expected = expected,
			.alternate = alternate,
			.length = length,
			.alternate_length = alt_length,
			.iterations = iterations,
			.failed = 0,
		};
		if (pthread_create(&threads[index], NULL, stress_thread,
				   &args[index]) != 0)
			return 2;
	}
	for (index = 0; index < 12; index++)
		pthread_join(threads[index], NULL);
	for (index = 0; index < 12; index++) {
		if (args[index].failed) {
			free(expected);
			free(alternate);
			return 1;
		}
	}
	free(expected);
	free(alternate);
	return 0;
}

static int check_proc(const char *path, const char *hex)
{
	unsigned char *expected;
	unsigned char *actual;
	size_t length;
	size_t offset;
	int descriptor;

	expected = decode_hex(hex, &length);
	actual = malloc(length + 1U);
	if (!expected || !actual) {
		free(expected);
		free(actual);
		return 2;
	}
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return 2;
	offset = 0;
	while (offset < length) {
		size_t chunk = offset % 7U + 1U;
		ssize_t result;

		if (chunk > length - offset)
			chunk = length - offset;
		result = read(descriptor, actual + offset, chunk);
		if (result != (ssize_t)chunk)
			return 1;
		offset += chunk;
	}
	if (read(descriptor, actual, 1) != 0 ||
	    memcmp(actual, expected, length) != 0)
		return 1;
	if (lseek(descriptor, 0, SEEK_SET) != 0 ||
	    read(descriptor, actual, length + 1U) != (ssize_t)length ||
	    memcmp(actual, expected, length) != 0 ||
	    read(descriptor, actual, 1) != 0)
		return 1;
	close(descriptor);
	free(expected);
	free(actual);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 4 && strcmp(argv[1], "write") == 0)
		return write_hex(argv[2], argv[3]);
	if (argc == 3 && strcmp(argv[1], "invalid") == 0)
		return invalid_read(argv[2]);
	if (argc == 5 && strcmp(argv[1], "error") == 0)
		return expect_error(argv[2], argv[3], argv[4]);
	if (argc == 5 && strcmp(argv[1], "position-error") == 0)
		return expect_position_error(argv[2], argv[3], argv[4]);
	if (argc == 7 && strcmp(argv[1], "check") == 0)
		return check_read(argv[2], argv[3], argv[4], argv[5], argv[6]);
	if (argc == 5 && strcmp(argv[1], "contains") == 0)
		return check_contains(argv[2], argv[3], argv[4], true);
	if (argc == 5 && strcmp(argv[1], "excludes") == 0)
		return check_contains(argv[2], argv[3], argv[4], false);
	if (argc == 4 && strcmp(argv[1], "proc") == 0)
		return check_proc(argv[2], argv[3]);
	if (argc == 5 && strcmp(argv[1], "stress") == 0)
		return stress(argv[2], argv[3], argv[4], NULL);
	if (argc == 6 && strcmp(argv[1], "stress") == 0)
		return stress(argv[2], argv[3], argv[4], argv[5]);
	usage(argv[0]);
	return 2;
}
#endif
