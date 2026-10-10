#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define SELECTED_PATH "/tmp/rootkitlings-rmdir-watch"
#define CONCURRENT_THREADS 8
#define CONCURRENT_ROUNDS 64

struct concurrent_case {
	struct start_gate *gate;
	atomic_int *failed;
	unsigned int index;
};

struct start_gate {
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	int started;
};

struct stress_case {
	struct timespec deadline;
	atomic_int *failed;
	unsigned int index;
};

static int before_deadline(const struct timespec *deadline)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	return now.tv_sec < deadline->tv_sec ||
	       (now.tv_sec == deadline->tv_sec &&
		now.tv_nsec < deadline->tv_nsec);
}

static int expect_rmdir_error(const char *path, int expected)
{
	errno = 0;
	if (syscall(SYS_rmdir, path) != -1 || errno != expected)
		return -1;
	return 0;
}

static int run_invalid_pointer(void)
{
	const char *invalid = (const char *)(uintptr_t)1;

	return expect_rmdir_error(invalid, EFAULT);
}

static int run_page_boundary(void)
{
	const long page_size = sysconf(_SC_PAGESIZE);
	const size_t selected_length = strlen(SELECTED_PATH);
	char *mapping;
	char *path;
	int result;

	if (page_size <= 0 || selected_length >= (size_t)page_size)
		return -1;
	mapping = mmap(NULL, (size_t)page_size * 2, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return -1;
	if (mprotect(mapping + page_size, (size_t)page_size, PROT_NONE) != 0) {
		munmap(mapping, (size_t)page_size * 2);
		return -1;
	}

	path = mapping + page_size - (long)selected_length;
	memcpy(path, SELECTED_PATH, selected_length);
	result = expect_rmdir_error(path, EFAULT);
	munmap(mapping, (size_t)page_size * 2);
	return result;
}

static void *run_concurrent_case(void *argument)
{
	struct concurrent_case *test = argument;
	char other_path[64];
	unsigned int round;

	if (test->index != 0) {
		if (snprintf(other_path, sizeof(other_path),
			     "/tmp/rootkitlings-rmdir-other-%u", test->index) < 0) {
			atomic_store(test->failed, 1);
			return NULL;
		}
		(void)rmdir(other_path);
	}
	if (pthread_mutex_lock(&test->gate->mutex) != 0) {
		atomic_store(test->failed, 1);
		return NULL;
	}
	while (!test->gate->started) {
		if (pthread_cond_wait(&test->gate->condition,
				      &test->gate->mutex) != 0) {
			atomic_store(test->failed, 1);
			pthread_mutex_unlock(&test->gate->mutex);
			return NULL;
		}
	}
	if (pthread_mutex_unlock(&test->gate->mutex) != 0) {
		atomic_store(test->failed, 1);
		return NULL;
	}

	for (round = 0; round < CONCURRENT_ROUNDS; round++) {
		if (test->index == 0) {
			if (mkdir(SELECTED_PATH, 0700) != 0 ||
			    syscall(SYS_rmdir, SELECTED_PATH) != 0) {
				atomic_store(test->failed, 1);
				break;
			}
		} else if (expect_rmdir_error(other_path, ENOENT) != 0) {
			atomic_store(test->failed, 1);
			break;
		}
	}
	return NULL;
}

static int run_concurrent(void)
{
	struct start_gate gate;
	pthread_t threads[CONCURRENT_THREADS];
	struct concurrent_case cases[CONCURRENT_THREADS];
	atomic_int failed = 0;
	unsigned int created = 0;
	unsigned int index;

	if (pthread_mutex_init(&gate.mutex, NULL) != 0)
		return -1;
	if (pthread_cond_init(&gate.condition, NULL) != 0) {
		pthread_mutex_destroy(&gate.mutex);
		return -1;
	}
	gate.started = 0;
	for (index = 0; index < CONCURRENT_THREADS; index++) {
		cases[index].gate = &gate;
		cases[index].failed = &failed;
		cases[index].index = index;
		if (pthread_create(&threads[index], NULL, run_concurrent_case,
				   &cases[index]) != 0) {
			atomic_store(&failed, 1);
			break;
		}
		created++;
	}
	if (pthread_mutex_lock(&gate.mutex) != 0)
		atomic_store(&failed, 1);
	else {
		gate.started = 1;
		if (pthread_cond_broadcast(&gate.condition) != 0)
			atomic_store(&failed, 1);
		if (pthread_mutex_unlock(&gate.mutex) != 0)
			atomic_store(&failed, 1);
	}
	for (index = 0; index < created; index++)
		pthread_join(threads[index], NULL);
	pthread_cond_destroy(&gate.condition);
	pthread_mutex_destroy(&gate.mutex);
	return atomic_load(&failed) == 0 ? 0 : -1;
}

static void *run_stress_case(void *argument)
{
	struct stress_case *test = argument;
	char path[64];

	if (snprintf(path, sizeof(path), "/tmp/rootkitlings-rmdir-stress-%u",
		     test->index) < 0) {
		atomic_store(test->failed, 1);
		return NULL;
	}
	(void)rmdir(path);
	while (before_deadline(&test->deadline)) {
		if (expect_rmdir_error(path, ENOENT) != 0) {
			atomic_store(test->failed, 1);
			break;
		}
	}
	return NULL;
}

static int run_stress(const char *seconds_text)
{
	pthread_t threads[CONCURRENT_THREADS];
	struct stress_case cases[CONCURRENT_THREADS];
	struct timespec deadline;
	atomic_int failed = 0;
	char *end = NULL;
	long seconds;
	unsigned int created = 0;
	unsigned int index;

	errno = 0;
	seconds = strtol(seconds_text, &end, 10);
	if (errno != 0 || !end || *end != '\0' || seconds < 1 || seconds > 10)
		return -1;
	if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
		return -1;
	deadline.tv_sec += seconds;
	for (index = 0; index < CONCURRENT_THREADS; index++) {
		cases[index].deadline = deadline;
		cases[index].failed = &failed;
		cases[index].index = index;
		if (pthread_create(&threads[index], NULL, run_stress_case,
				   &cases[index]) != 0) {
			atomic_store(&failed, 1);
			break;
		}
		created++;
	}
	for (index = 0; index < created; index++)
		pthread_join(threads[index], NULL);
	return created == CONCURRENT_THREADS && atomic_load(&failed) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
	int result = -1;

	if (argc == 2 && strcmp(argv[1], "invalid") == 0)
		result = run_invalid_pointer();
	else if (argc == 2 && strcmp(argv[1], "boundary") == 0)
		result = run_page_boundary();
	else if (argc == 2 && strcmp(argv[1], "concurrent") == 0)
		result = run_concurrent();
	else if (argc == 3 && strcmp(argv[1], "stress") == 0)
		result = run_stress(argv[2]);
	else {
		fprintf(stderr,
			"usage: %s invalid|boundary|concurrent|stress SECONDS\n",
			argv[0]);
		return 2;
	}
	if (result != 0) {
		fprintf(stderr, "rmdir telemetry test failed: %s\n", argv[1]);
		return 1;
	}
	return 0;
}
