#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/err.h>

#include "rootkit.h"

int rk_test_module_init(void);
void rk_test_module_exit(void);

static struct rk_state *create_result;
static int publish_result;
static char events[16];
static size_t event_count;
static struct rk_state *destroyed_pointer;

static void require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "[x] %s\n", message);
		exit(EXIT_FAILURE);
	}
}

static void record(char event)
{
	require(event_count + 1 < sizeof(events), "event trace overflow");
	events[event_count++] = event;
	events[event_count] = '\0';
}

static void clear_events(void)
{
	event_count = 0;
	events[0] = '\0';
	destroyed_pointer = NULL;
}

struct rk_state *rk_state_create(void)
{
	record('C');
	return create_result;
}

void rk_state_destroy(struct rk_state *state)
{
	record('D');
	destroyed_pointer = state;
}

int rk_proc_publish(void)
{
	record('P');
	return publish_result;
}

void rk_proc_unpublish(void)
{
	record('U');
}

int main(void)
{
	struct rk_state *token = (struct rk_state *)0x1000;
	int error;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT) {
		create_result = token;
		publish_result = 0;
		error = rk_test_module_init();
		require(error == -ECANCELED && event_count == 0,
			"preflight rejection must not acquire resources");
		puts("[+] module acquisition and rollback order verified");
		return EXIT_SUCCESS;
	}

	create_result = token;
	publish_result = 0;
	error = rk_test_module_init();
	require(error == 0 && strcmp(events, "CP") == 0,
		"init must create state before publishing procfs");
	rk_test_module_exit();
	require(strcmp(events, "CPUD") == 0 && destroyed_pointer == token,
		"exit must unpublish before destroying the acquired state");

	clear_events();
	create_result = ERR_PTR(-EAGAIN);
	error = rk_test_module_init();
	require(error == -EAGAIN && strcmp(events, "C") == 0,
		"state failure must preserve its error without publication or cleanup");

	clear_events();
	create_result = token;
	publish_result = -EACCES;
	error = rk_test_module_init();
	require(error == -EACCES && strcmp(events, "CPD") == 0,
		"publication failure must preserve its error and roll state back");
	require(destroyed_pointer == token,
		"publication rollback must destroy the acquired state object");

	puts("[+] module acquisition and rollback order verified");
	return EXIT_SUCCESS;
}
