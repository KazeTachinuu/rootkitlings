#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/err.h>

#include "rootkit.h"

int rk_test_module_init(void);
void rk_test_module_exit(void);

static struct rk_state *create_result;
static unsigned int create_calls;
static unsigned int destroy_calls;
static struct rk_state *destroyed_pointer;

static void require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "[x] %s\n", message);
		exit(EXIT_FAILURE);
	}
}

struct rk_state *rk_state_create(void)
{
	create_calls++;
	return create_result;
}

void rk_state_destroy(struct rk_state *state)
{
	destroy_calls++;
	destroyed_pointer = state;
}

int main(void)
{
	struct rk_state *token = (struct rk_state *)0x1000;
	int error;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT) {
		create_result = token;
		error = rk_test_module_init();
		require(error == -ECANCELED,
			"preflight rejection must return -ECANCELED");
		require(create_calls == 0 && destroy_calls == 0,
			"preflight rejection must not acquire or release state");
		puts("[+] module state lifecycle verified");
		return EXIT_SUCCESS;
	}

	create_result = token;
	error = rk_test_module_init();
	require(error == 0 && create_calls == 1,
		"successful init must create state exactly once");
	rk_test_module_exit();
	require(destroy_calls == 1 && destroyed_pointer == token,
		"module exit must destroy the object returned during init");

	create_result = ERR_PTR(-EAGAIN);
	error = rk_test_module_init();
	require(error == -EAGAIN,
		"init must preserve the constructor's exact error");
	require(create_calls == 2 && destroy_calls == 1,
		"failed construction must not call the destructor");

	puts("[+] module state lifecycle verified");
	return EXIT_SUCCESS;
}
