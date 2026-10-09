#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

struct linux_dirent64 {
	uint64_t d_ino;
	int64_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

struct reuse_context {
	int target;
	const char *proc_path;
	const char *ordinary_path;
	atomic_bool stop;
	atomic_int startup;
	atomic_bool start_race;
	atomic_ulong cycles;
};

static int replace_descriptor(int target, const char *path)
{
	int source = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	int result = 0;

	if (source < 0)
		return -1;
	if (dup2(source, target) < 0)
		result = -1;
	close(source);
	return result;
}

static void *reuse_descriptors(void *argument)
{
	struct reuse_context *context = argument;

	if (replace_descriptor(context->target, context->ordinary_path) < 0) {
		atomic_store_explicit(&context->startup, -1,
				      memory_order_release);
		return (void *)1;
	}
	atomic_store_explicit(&context->startup, 1, memory_order_release);
	while (
	    !atomic_load_explicit(&context->start_race, memory_order_acquire) &&
	    !atomic_load_explicit(&context->stop, memory_order_relaxed))
		sched_yield();
	while (!atomic_load_explicit(&context->stop, memory_order_relaxed)) {
		if (replace_descriptor(context->target,
				       context->ordinary_path) < 0)
			return (void *)1;
		sched_yield();
		if (replace_descriptor(context->target, context->proc_path) < 0)
			return (void *)1;
		atomic_fetch_add_explicit(&context->cycles, 1,
					  memory_order_relaxed);
		sched_yield();
	}
	return NULL;
}

static int inspect_chunk(const char *buffer, size_t count, const char *target,
			 bool *ordinary_marker, bool *target_seen)
{
	size_t offset = 0;

	*ordinary_marker = false;
	*target_seen = false;
	while (offset < count) {
		const struct linux_dirent64 *entry =
		    (const void *)(buffer + offset);
		size_t name_capacity;

		if (count - offset <
			offsetof(struct linux_dirent64, d_name) + 1 ||
		    entry->d_reclen <
			offsetof(struct linux_dirent64, d_name) + 1 ||
		    entry->d_reclen > count - offset)
			return -1;
		name_capacity =
		    entry->d_reclen - offsetof(struct linux_dirent64, d_name);
		if (!memchr(entry->d_name, '\0', name_capacity))
			return -1;
		if (strcmp(entry->d_name, "ordinary_marker") == 0)
			*ordinary_marker = true;
		if (strcmp(entry->d_name, target) == 0)
			*target_seen = true;
		offset += entry->d_reclen;
	}
	return offset == count ? 0 : -1;
}

static int fd_reuse_stress(const char *proc_path, const char *ordinary_path,
			   unsigned long iterations, const char *target)
{
	char buffer[32768];
	struct reuse_context context;
	pthread_t thread;
	void *thread_result;
	unsigned long iteration;
	unsigned long ordinary_chunks = 0;
	unsigned long cycles;
	int startup;
	int result = 0;

	context.proc_path = proc_path;
	context.ordinary_path = ordinary_path;
	context.target = open(proc_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (context.target < 0)
		return 2;
	atomic_init(&context.stop, false);
	atomic_init(&context.startup, 0);
	atomic_init(&context.start_race, false);
	atomic_init(&context.cycles, 0);
	if (pthread_create(&thread, NULL, reuse_descriptors, &context) != 0) {
		close(context.target);
		return 2;
	}
	do {
		startup = atomic_load_explicit(&context.startup,
					       memory_order_acquire);
		if (startup == 0)
			sched_yield();
	} while (startup == 0);
	if (startup < 0) {
		pthread_join(thread, &thread_result);
		close(context.target);
		return 2;
	}
	if (lseek(context.target, 0, SEEK_SET) < 0) {
		result = 2;
	} else {
		long count = syscall(SYS_getdents64, context.target, buffer,
				     sizeof(buffer));
		bool ordinary_marker;
		bool target_seen;

		if (count <= 0 ||
		    inspect_chunk(buffer, (size_t)count, target,
				  &ordinary_marker, &target_seen) < 0)
			result = 3;
		else if (!ordinary_marker || !target_seen)
			result = 4;
		else
			ordinary_chunks++;
	}
	atomic_store_explicit(&context.start_race, true, memory_order_release);
	for (iteration = 0; result == 0 && iteration < iterations;
	     iteration++) {
		long count;
		bool ordinary_marker;
		bool target_seen;

		(void)lseek(context.target, 0, SEEK_SET);
		count = syscall(SYS_getdents64, context.target, buffer,
				sizeof(buffer));
		if (count <= 0)
			continue;
		if (inspect_chunk(buffer, (size_t)count, target,
				  &ordinary_marker, &target_seen) < 0) {
			result = 3;
			break;
		}
		if (ordinary_marker && target_seen)
			ordinary_chunks++;
		sched_yield();
	}
	atomic_store_explicit(&context.stop, true, memory_order_relaxed);
	pthread_join(thread, &thread_result);
	if (thread_result)
		result = 5;
	cycles = atomic_load_explicit(&context.cycles, memory_order_relaxed);
	if (result == 0 && cycles == 0)
		result = 6;
	close(context.target);
	if (result == 0)
		printf("fd_reuse_iterations=%lu ordinary_chunks=%lu "
		       "replacement_cycles=%lu\n",
		       iterations, ordinary_chunks, cycles);
	return result;
}

static int enumerate(const char *path, size_t capacity, const char *target,
		     int require_target, bool verify_cookies)
{
	char *buffer;
	int descriptor;
	bool found = false;
	unsigned int chunks = 0;

	descriptor = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (descriptor < 0)
		return 2;
	buffer = malloc(capacity);
	if (!buffer) {
		close(descriptor);
		return 2;
	}
	for (;;) {
		long count =
		    syscall(SYS_getdents64, descriptor, buffer, capacity);
		size_t offset = 0;

		if (count < 0) {
			free(buffer);
			close(descriptor);
			return 2;
		}
		if (count == 0)
			break;
		chunks++;
		while (offset < (size_t)count) {
			struct linux_dirent64 *entry =
			    (void *)(buffer + offset);
			size_t name_capacity;

			if ((size_t)count - offset <
				offsetof(struct linux_dirent64, d_name) + 1 ||
			    entry->d_reclen <
				offsetof(struct linux_dirent64, d_name) + 1 ||
			    entry->d_reclen > (size_t)count - offset) {
				free(buffer);
				close(descriptor);
				return 3;
			}
			name_capacity = entry->d_reclen -
					offsetof(struct linux_dirent64, d_name);
			if (!memchr(entry->d_name, '\0', name_capacity)) {
				free(buffer);
				close(descriptor);
				return 3;
			}
			if (strcmp(entry->d_name, target) == 0)
				found = true;
			if (verify_cookies) {
				off_t cookie = entry->d_off;
				if (lseek(descriptor, cookie, SEEK_SET) < 0) {
					free(buffer);
					close(descriptor);
					return 4;
				}
				if (lseek(descriptor, 0, SEEK_CUR) != cookie) {
					free(buffer);
					close(descriptor);
					return 4;
				}
			}
			offset += entry->d_reclen;
		}
		if (offset != (size_t)count) {
			free(buffer);
			close(descriptor);
			return 3;
		}
	}
	printf("found=%u chunks=%u\n", found ? 1U : 0U, chunks);
	free(buffer);
	close(descriptor);
	return require_target < 0 || found == (require_target != 0) ? 0 : 1;
}

static int write_all(int descriptor, const char *text)
{
	size_t length = strlen(text);
	ssize_t written = write(descriptor, text, length);
	return written == (ssize_t)length ? 0 : -1;
}

static int isolate_all_match(const char *path, const char *control,
			     const char *target)
{
	char buffer[4096];
	off_t target_cookie = 0;
	unsigned short target_length = 0;
	off_t preceding_cookie = 0;
	int directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	int writer;

	if (directory < 0)
		return 2;
	for (;;) {
		long count =
		    syscall(SYS_getdents64, directory, buffer, sizeof(buffer));
		size_t offset = 0;
		if (count <= 0) {
			close(directory);
			return 3;
		}
		while (offset < (size_t)count) {
			struct linux_dirent64 *entry =
			    (void *)(buffer + offset);
			if (strcmp(entry->d_name, target) == 0) {
				target_cookie = preceding_cookie;
				target_length = entry->d_reclen;
				goto found;
			}
			preceding_cookie = entry->d_off;
			offset += entry->d_reclen;
		}
	}
found:
	writer = open(control, O_WRONLY | O_CLOEXEC);
	if (writer < 0 || write_all(writer, "enable") != 0) {
		if (writer >= 0)
			close(writer);
		close(directory);
		return 4;
	}
	close(writer);
	if (lseek(directory, target_cookie, SEEK_SET) != target_cookie) {
		close(directory);
		return 5;
	}
	{
		long count =
		    syscall(SYS_getdents64, directory, buffer, target_length);
		struct linux_dirent64 *entry = (void *)buffer;
		if (count <= 0 || count != target_length ||
		    strcmp(entry->d_name, target) != 0) {
			close(directory);
			return 6;
		}
		count =
		    syscall(SYS_getdents64, directory, buffer, sizeof(buffer));
		if (count < 0 ||
		    (count > 0 &&
		     strcmp(((struct linux_dirent64 *)buffer)->d_name,
			    target) == 0)) {
			close(directory);
			return 7;
		}
	}
	puts("all_match_positive=1 progressed=1");
	close(directory);
	return 0;
}

static void usage(const char *name)
{
	fprintf(stderr,
		"usage: %s visible|hidden|either|cookies PATH CAPACITY NAME\n"
		"       %s isolate PATH CONTROL NAME\n"
		"       %s fdreuse PROC_PATH ORDINARY_PATH ITERATIONS NAME\n",
		name, name, name);
}

int main(int argc, char **argv)
{
	char *end;
	unsigned long capacity;
	int require_target;
	bool verify_cookies = false;

	if (argc == 6 && strcmp(argv[1], "fdreuse") == 0) {
		errno = 0;
		capacity = strtoul(argv[4], &end, 10);
		if (errno || *end != '\0' || capacity == 0)
			return 2;
		return fd_reuse_stress(argv[2], argv[3], capacity, argv[5]);
	}
	if (argc == 5 && strcmp(argv[1], "isolate") == 0)
		return isolate_all_match(argv[2], argv[3], argv[4]);
	if (argc != 5) {
		usage(argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "visible") == 0)
		require_target = true;
	else if (strcmp(argv[1], "hidden") == 0)
		require_target = false;
	else if (strcmp(argv[1], "either") == 0)
		require_target = -1;
	else if (strcmp(argv[1], "cookies") == 0) {
		require_target = false;
		verify_cookies = true;
	} else {
		usage(argv[0]);
		return 2;
	}
	errno = 0;
	capacity = strtoul(argv[3], &end, 10);
	if (errno || *end != '\0' || capacity < 24 || capacity > 131072)
		return 2;
	return enumerate(argv[2], capacity, argv[4], require_target,
			 verify_cookies);
}
