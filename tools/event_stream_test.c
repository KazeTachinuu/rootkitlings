// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE 8192
#define WORKERS 24
#define SELECTED_PATH "/tmp/rootkitlings-rmdir-watch"

struct result {
	pid_t tid;
	int error;
};

static ssize_t read_all(int fd, char *buffer, size_t capacity)
{
	size_t used = 0;

	while (used + 1 < capacity) {
		ssize_t count = read(fd, buffer + used, capacity - used - 1);

		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0)
			return -1;
		if (count == 0)
			break;
		used += (size_t)count;
	}
	buffer[used] = '\0';
	return (ssize_t)used;
}

static int selected_success(void)
{
	if (mkdir(SELECTED_PATH, 0700) < 0 && errno != EEXIST)
		return -1;
	return rmdir(SELECTED_PATH);
}

static int check_snapshot(const char *events)
{
	char first[BUFFER_SIZE];
	char refreshed[BUFFER_SIZE];
	char byte;
	ssize_t remainder;
	int fd;

	unlink(SELECTED_PATH);
	fd = open(events, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || selected_success() < 0)
		return 1;
	if (read(fd, &byte, 1) != 1)
		return 1;
	first[0] = byte;
	errno = 0;
	if (rmdir(SELECTED_PATH) == 0 || errno != ENOENT)
		return 1;
	remainder = read_all(fd, first + 1, sizeof(first) - 1);
	if (remainder < 0)
		return 1;
	if (strstr(first, "result=success") == NULL ||
	    strstr(first, "result=failure") != NULL)
		return 1;
	if (selected_success() < 0 || read(fd, &byte, 1) != 0)
		return 1;
	if (lseek(fd, 0, SEEK_SET) != 0 ||
	    read_all(fd, refreshed, sizeof(refreshed)) < 0)
		return 1;
	close(fd);
	if (strstr(refreshed, "sequence=1 kind=rmdir") == NULL ||
	    strstr(refreshed, "sequence=2 kind=rmdir") == NULL ||
	    strstr(refreshed, "sequence=3 kind=rmdir") == NULL ||
	    strstr(refreshed, "result=failure") == NULL)
		return 1;
	return 0;
}

static int write_command(const char *control, const char *token)
{
	char command[64];
	ssize_t count;
	int fd;
	int length;

	length = snprintf(command, sizeof(command), "root %s", token);
	fd = open(control, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return errno;
	count = write(fd, command, (size_t)length);
	if (count != length) {
		int saved = count < 0 ? errno : EIO;

		close(fd);
		return saved;
	}
	close(fd);
	return 0;
}

static int check_credential_race(const char *control, const char *events,
				 const char *token)
{
	struct result results[WORKERS];
	pid_t children[WORKERS];
	char record[BUFFER_SIZE];
	char expected[96];
	int start_pipe[2];
	int result_pipe[2];
	int successes = 0;
	pid_t winner = -1;
	int index;
	int fd;

	if (pipe(start_pipe) < 0 || pipe(result_pipe) < 0)
		return 1;
	for (index = 0; index < WORKERS; index++) {
		children[index] = fork();
		if (children[index] == 0) {
			struct result result;
			char byte;

			close(start_pipe[1]);
			close(result_pipe[0]);
			if (read(start_pipe[0], &byte, 1) != 1)
				_exit(2);
			if (setgid(65534) < 0 || setuid(65534) < 0)
				_exit(2);
			result.tid = getpid();
			result.error = write_command(control, token);
			if (write(result_pipe[1], &result, sizeof(result)) !=
			    (ssize_t)sizeof(result))
				_exit(2);
			_exit(0);
		}
		if (children[index] < 0)
			return 1;
	}
	close(start_pipe[0]);
	close(result_pipe[1]);
	for (index = 0; index < WORKERS; index++)
		if (write(start_pipe[1], "x", 1) != 1)
			return 1;
	close(start_pipe[1]);
	for (index = 0; index < WORKERS; index++) {
		ssize_t count = read(result_pipe[0], &results[index],
				     sizeof(results[index]));

		if (count != (ssize_t)sizeof(results[index]))
			return 1;
		if (results[index].error == 0) {
			successes++;
			winner = results[index].tid;
		} else if (results[index].error != EACCES) {
			return 1;
		}
	}
	close(result_pipe[0]);
	for (index = 0; index < WORKERS; index++) {
		int status;

		if (waitpid(children[index], &status, 0) != children[index] ||
		    !WIFEXITED(status) || WEXITSTATUS(status) != 0)
			return 1;
	}
	if (successes != 1)
		return 1;
	fd = open(events, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || read_all(fd, record, sizeof(record)) < 0)
		return 1;
	close(fd);
	snprintf(expected, sizeof(expected), "kind=credential-change tid=%d", winner);
	return strstr(record, expected) != NULL ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "snapshot") == 0)
		return check_snapshot(argv[2]);
	if (argc == 5 && strcmp(argv[1], "credential-race") == 0)
		return check_credential_race(argv[2], argv[3], argv[4]);
	fprintf(stderr,
		"usage: %s snapshot EVENTS | credential-race CONTROL EVENTS TOKEN\n",
		argv[0]);
	return 2;
}
