// SPDX-License-Identifier: GPL-2.0
// Verify which task materializes a procfs seq_file record.
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
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

#define COMM_SIZE 16U
#define RECORD_SIZE 256U

struct thread_case {
	int fd;
	const unsigned char *name;
	unsigned char expected[RECORD_SIZE];
	size_t expected_length;
	unsigned char first_byte;
	int result;
};

static const unsigned char inherited_name[] = {
	'i', 'n', 'h', '"', '\\', '\n', 0x7f, 0
};

static const unsigned char hostile_thread_name[] = {
	't', 'h', 'r', '"', '\\', '\n', '\t', 0x7f, 0x80, ' ', 'Z', 0
};

static const unsigned char rewind_name[] = {
	'r', 'e', 'w', '"', '\\', '\r', 0x81, 0
};

static pid_t gettid_value(void)
{
	return (pid_t)syscall(SYS_gettid);
}

static int set_and_get_name(const unsigned char *name,
			    unsigned char comm[COMM_SIZE])
{
	if (prctl(PR_SET_NAME, name, 0UL, 0UL, 0UL) < 0) {
		fprintf(stderr, "task_attribution_test: PR_SET_NAME: %s\n",
			strerror(errno));
		return -1;
	}
	memset(comm, 0, COMM_SIZE);
	if (prctl(PR_GET_NAME, comm, 0UL, 0UL, 0UL) < 0) {
		fprintf(stderr, "task_attribution_test: PR_GET_NAME: %s\n",
			strerror(errno));
		return -1;
	}
	comm[COMM_SIZE - 1] = 0;
	return 0;
}

static int append_bytes(unsigned char *record, size_t *used,
			const void *source, size_t length)
{
	if (length > RECORD_SIZE - *used)
		return -1;
	memcpy(record + *used, source, length);
	*used += length;
	return 0;
}

static int append_text(unsigned char *record, size_t *used, const char *text)
{
	return append_bytes(record, used, text, strlen(text));
}

static int build_expected(unsigned char record[RECORD_SIZE], size_t *length,
			  pid_t tid, const unsigned char comm[COMM_SIZE])
{
	static const char hex[] = "0123456789abcdef";
	char number[32];
	size_t used = 0;
	size_t index;
	int count;

	if (append_text(record, &used, "active=1\nreader_tid=") < 0)
		return -1;
	count = snprintf(number, sizeof(number), "%ld", (long)tid);
	if (count < 1 || (size_t)count >= sizeof(number) ||
	    append_bytes(record, &used, number, (size_t)count) < 0 ||
	    append_text(record, &used, "\nreader_comm=\"") < 0)
		return -1;

	for (index = 0; index < COMM_SIZE && comm[index] != 0; index++) {
		const unsigned char byte = comm[index];

		if (byte >= 0x20 && byte <= 0x7e && byte != '"' && byte != '\\') {
			if (append_bytes(record, &used, &byte, 1) < 0)
				return -1;
		} else {
			const unsigned char escaped[] = {
				'\\', 'x', hex[byte >> 4], hex[byte & 0x0f]
			};

			if (append_bytes(record, &used, escaped, sizeof(escaped)) < 0)
				return -1;
		}
	}
	if (append_text(record, &used, "\"\n") < 0)
		return -1;
	*length = used;
	return 0;
}

static ssize_t read_retry(int fd, void *buffer, size_t length)
{
	ssize_t count;

	do {
		count = read(fd, buffer, length);
	} while (count < 0 && errno == EINTR);
	return count;
}

static int read_record(int fd, unsigned char record[RECORD_SIZE], size_t *length,
			const char *phase)
{
	static const size_t pattern[] = { 1, 3, 2, 7, 4, 11 };
	size_t used = 0;
	size_t step = 0;

	for (;;) {
		ssize_t count;
		size_t requested = pattern[step % (sizeof(pattern) / sizeof(pattern[0]))];

		if (requested > RECORD_SIZE - used)
			requested = RECORD_SIZE - used;
		if (requested == 0) {
			fprintf(stderr,
				"task_attribution_test: %s exceeds %u bytes\n",
				phase, RECORD_SIZE);
			return -1;
		}
		count = read_retry(fd, record + used, requested);
		if (count < 0) {
			fprintf(stderr, "task_attribution_test: %s read: %s\n",
				phase, strerror(errno));
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
			if (count < 0)
				fprintf(stderr,
					"task_attribution_test: %s repeated EOF: %s\n",
					phase, strerror(errno));
			else
				fprintf(stderr,
					"task_attribution_test: %s returned data after EOF\n",
					phase);
			return -1;
		}
	}
	*length = used;
	return 0;
}

static int compare_record(const unsigned char *actual, size_t actual_length,
			  const unsigned char *expected, size_t expected_length,
			  const char *phase)
{
	if (actual_length != expected_length ||
	    memcmp(actual, expected, expected_length) != 0) {
		fprintf(stderr,
			"task_attribution_test: %s record differs (got %zu bytes, expected %zu)\n",
			phase, actual_length, expected_length);
		return -1;
	}
	return 0;
}

static int verify_current_record(int fd, const unsigned char *name,
				 const char *phase)
{
	unsigned char actual[RECORD_SIZE];
	unsigned char comm[COMM_SIZE];
	unsigned char expected[RECORD_SIZE];
	size_t actual_length;
	size_t expected_length;

	if (set_and_get_name(name, comm) < 0 ||
	    build_expected(expected, &expected_length, gettid_value(), comm) < 0 ||
	    read_record(fd, actual, &actual_length, phase) < 0)
		return -1;
	return compare_record(actual, actual_length, expected, expected_length,
			      phase);
}

static int verify_inherited_descriptor(const char *path)
{
	pid_t child;
	int status;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "task_attribution_test: open inherited descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	child = fork();
	if (child < 0) {
		fprintf(stderr, "task_attribution_test: fork: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	if (child == 0) {
		int result = verify_current_record(fd, inherited_name,
						   "inherited descriptor");

		close(fd);
		_exit(result == 0 ? 0 : 1);
	}
	if (close(fd) < 0) {
		fprintf(stderr,
			"task_attribution_test: close inherited descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	if (waitpid(child, &status, 0) != child) {
		fprintf(stderr, "task_attribution_test: waitpid: %s\n",
			strerror(errno));
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr,
			"task_attribution_test: inherited-descriptor child failed\n");
		return -1;
	}
	return 0;
}

static void *verify_nonleader_thread(void *opaque)
{
	struct thread_case *test = opaque;

	if (gettid_value() == getpid()) {
		fprintf(stderr,
			"task_attribution_test: worker unexpectedly has the process leader ID\n");
		test->result = -1;
		return NULL;
	}
	test->result = verify_current_record(test->fd, test->name,
					     "nonleader thread");
	return NULL;
}

static int verify_thread_descriptor(const char *path)
{
	struct thread_case test = { .fd = -1, .name = hostile_thread_name };
	pthread_t thread;
	int error;

	test.fd = open(path, O_RDONLY | O_CLOEXEC);
	if (test.fd < 0) {
		fprintf(stderr, "task_attribution_test: open thread descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	error = pthread_create(&thread, NULL, verify_nonleader_thread, &test);
	if (error != 0) {
		fprintf(stderr, "task_attribution_test: pthread_create: %s\n",
			strerror(error));
		close(test.fd);
		return -1;
	}
	error = pthread_join(thread, NULL);
	if (error != 0) {
		fprintf(stderr, "task_attribution_test: pthread_join: %s\n",
			strerror(error));
		close(test.fd);
		return -1;
	}
	if (close(test.fd) < 0) {
		fprintf(stderr, "task_attribution_test: close thread descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	return test.result;
}

static void *materialize_one_byte(void *opaque)
{
	struct thread_case *test = opaque;
	unsigned char comm[COMM_SIZE];
	ssize_t count;

	if (gettid_value() == getpid() ||
	    set_and_get_name(test->name, comm) < 0 ||
	    build_expected(test->expected, &test->expected_length,
			   gettid_value(), comm) < 0) {
		test->result = -1;
		return NULL;
	}
	count = read_retry(test->fd, &test->first_byte, 1);
	if (count != 1) {
		if (count < 0)
			fprintf(stderr,
				"task_attribution_test: buffered prefix read: %s\n",
				strerror(errno));
		else
			fprintf(stderr,
				"task_attribution_test: buffered prefix returned %zd bytes\n",
				count);
		test->result = -1;
	}
	return NULL;
}

static int verify_buffer_and_rewind(const char *path)
{
	struct thread_case test = { .fd = -1, .name = hostile_thread_name };
	unsigned char actual[RECORD_SIZE];
	unsigned char tail[RECORD_SIZE];
	size_t actual_length;
	size_t tail_length;
	pthread_t thread;
	int error;

	test.fd = open(path, O_RDONLY | O_CLOEXEC);
	if (test.fd < 0) {
		fprintf(stderr, "task_attribution_test: open buffered descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	error = pthread_create(&thread, NULL, materialize_one_byte, &test);
	if (error != 0) {
		fprintf(stderr, "task_attribution_test: pthread_create: %s\n",
			strerror(error));
		close(test.fd);
		return -1;
	}
	error = pthread_join(thread, NULL);
	if (error != 0 || test.result != 0) {
		if (error != 0)
			fprintf(stderr, "task_attribution_test: pthread_join: %s\n",
				strerror(error));
		close(test.fd);
		return -1;
	}
	if (read_record(test.fd, tail, &tail_length,
			"buffered record remainder") < 0 ||
	    tail_length >= RECORD_SIZE) {
		close(test.fd);
		return -1;
	}
	actual[0] = test.first_byte;
	memcpy(actual + 1, tail, tail_length);
	actual_length = tail_length + 1;
	if (compare_record(actual, actual_length, test.expected,
			   test.expected_length, "buffered record") < 0) {
		close(test.fd);
		return -1;
	}
	if (lseek(test.fd, 0, SEEK_SET) != 0) {
		fprintf(stderr, "task_attribution_test: rewind: %s\n",
			strerror(errno));
		close(test.fd);
		return -1;
	}
	if (verify_current_record(test.fd, rewind_name, "record after rewind") < 0) {
		close(test.fd);
		return -1;
	}
	if (close(test.fd) < 0) {
		fprintf(stderr, "task_attribution_test: close buffered descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static int verify_write_rejected(const char *path)
{
	const unsigned char byte = 'x';
	ssize_t count;
	int fd = open(path, O_WRONLY | O_CLOEXEC);

	if (fd < 0)
		return 0;
	count = write(fd, &byte, 1);
	if (count < 0 && errno == EINTR)
		count = write(fd, &byte, 1);
	if (close(fd) < 0) {
		fprintf(stderr, "task_attribution_test: close write descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	if (count >= 0) {
		fprintf(stderr, "task_attribution_test: proc entry accepted a write\n");
		return -1;
	}
	return 0;
}

static int verify_unload(const char *path, const char *module)
{
	unsigned char byte;
	ssize_t count;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "task_attribution_test: open unload descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	count = read_retry(fd, &byte, 1);
	if (count != 1) {
		fprintf(stderr,
			"task_attribution_test: unload prefix returned %zd bytes\n",
			count);
		close(fd);
		return -1;
	}
	if (syscall(SYS_delete_module, module, 0) < 0) {
		fprintf(stderr, "task_attribution_test: delete module %s: %s\n",
			module, strerror(errno));
		close(fd);
		return -1;
	}
	count = read_retry(fd, &byte, 1);
	if (count > 0) {
		fprintf(stderr,
			"task_attribution_test: stale descriptor returned data after unload\n");
		close(fd);
		return -1;
	}
	if (close(fd) < 0) {
		fprintf(stderr, "task_attribution_test: close unload descriptor: %s\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct stat status;

	if (argc != 3) {
		fprintf(stderr, "usage: %s PATH MODULE\n", argv[0]);
		return 2;
	}
	if (verify_inherited_descriptor(argv[1]) < 0 ||
	    verify_thread_descriptor(argv[1]) < 0 ||
	    verify_buffer_and_rewind(argv[1]) < 0 ||
	    verify_write_rejected(argv[1]) < 0 ||
	    verify_unload(argv[1], argv[2]) < 0)
		return 1;
	if (stat(argv[1], &status) == 0 || errno != ENOENT) {
		fprintf(stderr,
			"task_attribution_test: proc entry remains after module unload\n");
		return 1;
	}
	printf("task_attribution_test: verified callback attribution and seq_file lifetime\n");
	return 0;
}
