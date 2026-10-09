#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/err.h>
#include <linux/slab.h>

#include "rootkit.h"

static unsigned char storage[64];
static bool allocation_available;
static unsigned int allocation_calls;
static unsigned int free_calls;
static size_t requested_size;
static unsigned int requested_flags;
static const void *freed_pointer;

static void require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "[x] %s\n", message);
		exit(EXIT_FAILURE);
	}
}

void *rk_test_kzalloc(size_t size, unsigned int flags)
{
	allocation_calls++;
	requested_size = size;
	requested_flags = flags;
	if (!allocation_available)
		return NULL;
	memset(storage, 0, sizeof(storage));
	return storage;
}

void rk_test_kfree(const void *pointer)
{
	free_calls++;
	freed_pointer = pointer;
}

int rk_test_warn_on(int condition)
{
	return condition;
}

int main(void)
{
	struct rk_state *state;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_STATE_ALLOC) {
		allocation_available = true;
		state = rk_state_create();
		require(IS_ERR(state) && PTR_ERR(state) == -ENOMEM,
			"injected state failure must return ERR_PTR(-ENOMEM)");
		require(allocation_calls == 0,
			"injected state failure must not allocate first");
		puts("[+] state ownership behavior verified");
		return EXIT_SUCCESS;
	}

	allocation_available = true;
	state = rk_state_create();
	require(!IS_ERR(state) && state == (struct rk_state *)storage,
		"successful create must return the allocated object");
	require(allocation_calls == 1 && requested_size > 0,
		"successful create must allocate one non-empty object");
	require(requested_flags == GFP_KERNEL,
		"state allocation must use GFP_KERNEL");
	rk_state_destroy(state);
	require(free_calls == 1 && freed_pointer == state,
		"destroy must free the exact object returned by create");

	allocation_available = false;
	state = rk_state_create();
	require(IS_ERR(state) && PTR_ERR(state) == -ENOMEM,
		"allocator failure must return ERR_PTR(-ENOMEM)");
	require(allocation_calls == 2 && free_calls == 1,
		"failed creation must not free or publish an object");

	puts("[+] state ownership behavior verified");
	return EXIT_SUCCESS;
}
