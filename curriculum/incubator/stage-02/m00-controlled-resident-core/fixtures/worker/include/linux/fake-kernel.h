#ifndef ROOTKITLINGS_FAKE_KERNEL_H
#define ROOTKITLINGS_FAKE_KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define __must_check

#ifndef ROOTKITLINGS_FAIL_POINT
#define ROOTKITLINGS_FAIL_POINT 0
#endif

#define RK_FAIL_WORKER_CREATE 4

#define EAGAIN 11
#define EACCES 13
#define EINTR 4

struct rk_state {
	bool enabled;
	unsigned long runs;
};

struct task_struct {
	int unused;
};

typedef struct {
	int value;
} atomic_t;

typedef struct {
	int unused;
} wait_queue_head_t;

extern struct task_struct fake_task;
extern int fake_stop_after;
extern int fake_stop_calls;
extern int fake_wake_calls;
extern int fake_join_calls;

static inline void atomic_set(atomic_t *value, int next)
{
	value->value = next;
}

static inline int atomic_read(const atomic_t *value)
{
	return value->value;
}

static inline int atomic_xchg(atomic_t *value, int next)
{
	int previous = value->value;

	value->value = next;
	return previous;
}

static inline int atomic_cmpxchg(atomic_t *value, int expected, int next)
{
	int previous = value->value;

	if (previous == expected)
		value->value = next;
	return previous;
}

static inline void init_waitqueue_head(wait_queue_head_t *queue)
{
	queue->unused = 0;
}

static inline void wake_up_interruptible(wait_queue_head_t *queue)
{
	(void)queue;
	fake_wake_calls++;
}

static inline bool kthread_should_stop(void)
{
	return fake_stop_calls++ >= fake_stop_after;
}

static inline int fake_wait_event(bool condition)
{
	return condition ? 0 : -1;
}

#define wait_event_interruptible(queue, condition) fake_wait_event(condition)

static inline struct task_struct *fake_kthread_run(void)
{
	return &fake_task;
}

#define kthread_run(function, argument, name) fake_kthread_run()

static inline int kthread_stop(struct task_struct *task)
{
	(void)task;
	fake_join_calls++;
	return 0;
}

#define ERR_PTR(error) ((struct task_struct *)(intptr_t)(error))
#define PTR_ERR(pointer) ((long)(intptr_t)(pointer))
#define IS_ERR(pointer) ((uintptr_t)(pointer) >= (uintptr_t)-4095)

static inline bool rk_state_actions_enabled(const struct rk_state *state)
{
	return state && state->enabled;
}

static inline void rk_state_record_worker_run(struct rk_state *state)
{
	state->runs++;
}

int rk_worker_request(void);
int rk_worker_start(struct rk_state *state);
void rk_worker_stop(void);

#endif
