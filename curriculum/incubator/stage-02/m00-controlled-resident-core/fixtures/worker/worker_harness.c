#include <stdio.h>
#include <stdlib.h>

#include "linux/fake-kernel.h"

#ifndef WORKER_SOURCE
#error "WORKER_SOURCE must name the submitted execution/worker.c"
#endif

#include WORKER_SOURCE

struct task_struct fake_task;
int fake_stop_after;
int fake_stop_calls;
int fake_wake_calls;
int fake_join_calls;

static void require(bool condition, const char *message)
{
	if (condition)
		return;
	fprintf(stderr, "[x] %s\n", message);
	exit(1);
}

static void reset_fakes(void)
{
	fake_stop_after = 0;
	fake_stop_calls = 0;
	fake_wake_calls = 0;
	fake_join_calls = 0;
}

#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_WORKER_CREATE
int main(void)
{
	struct rk_state state = { .enabled = true };

	reset_fakes();
	require(rk_worker_start(&state) == -EINTR,
		"worker creation failure must return EINTR");
	require(worker_state == NULL, "failed start must release its state pointer");
	require(worker_task == NULL, "failed start must release its task pointer");
	require(fake_join_calls == 0, "failed start must not join a task that never started");
	return 0;
}
#else
int main(void)
{
	struct rk_state disabled = { .enabled = false };
	struct rk_state enabled = { .enabled = true };

	reset_fakes();
	require(rk_worker_start(&disabled) == 0, "worker start must succeed");
	require(atomic_read(&pending) == 0, "worker start must empty the request slot");
	require(rk_worker_request() == -EACCES,
		"a disabled action gate must reject a request");
	require(atomic_read(&pending) == 0,
		"a rejected request must not occupy the slot");
	require(fake_wake_calls == 0, "a rejected request must not wake the worker");

	worker_state = &enabled;
	require(rk_worker_request() == 0, "an empty enabled slot must admit one request");
	require(atomic_read(&pending) == 1, "an admitted request must occupy the slot");
	require(fake_wake_calls == 1, "an admitted request must wake the worker once");
	require(rk_worker_request() == -EAGAIN,
		"a full slot must reject a second request with EAGAIN");
	require(atomic_read(&pending) == 1, "a rejected second request must preserve the slot");
	require(fake_wake_calls == 1, "a rejected second request must not wake the worker");

	fake_stop_after = 1;
	fake_stop_calls = 0;
	require(rk_worker_main(NULL) == 0, "the worker must stop cleanly");
	require(enabled.runs == 1, "the worker must execute one admitted enabled request");
	require(atomic_read(&pending) == 0, "the worker must consume the occupied slot");

	atomic_set(&pending, 1);
	enabled.enabled = false;
	fake_stop_after = 1;
	fake_stop_calls = 0;
	require(rk_worker_main(NULL) == 0, "the disabled worker must stop cleanly");
	require(enabled.runs == 1,
		"the worker must discard an admitted request after the gate is disabled");
	require(atomic_read(&pending) == 0, "discarding a request must clear the slot");

	atomic_set(&pending, 1);
	enabled.enabled = true;
	fake_stop_after = 0;
	fake_stop_calls = 0;
	require(rk_worker_main(NULL) == 0, "a requested stop must end the worker");
	require(enabled.runs == 1, "stop must take priority over pending work");
	require(atomic_read(&pending) == 1, "stop must not consume pending work first");

	rk_worker_stop();
	require(fake_join_calls == 1, "worker stop must join the resident task once");
	require(worker_state == NULL, "worker stop must release its state pointer");
	require(worker_task == NULL, "worker stop must release its task pointer");
	require(atomic_read(&pending) == 0, "worker stop must empty the request slot");
	return 0;
}
#endif
