# Wake a sleeping kernel worker

An operator command should not trap the proc writer inside long-running kernel
work.
Add `run` to wake a dedicated kernel thread, let that worker complete one unit,
then put it back to sleep.
Success proves that accepted commands are neither lost nor merged and that the
worker stops before its state is freed.

A **kernel thread** is a task created by the kernel rather than a userspace
program.
The proc callback only validates and queues bounded requests.

The complete grammar is `enable`, `disable`, or `run`, each with one optional
final newline.
`run` returns `-EACCES` while actions are disabled.
Once enabled, each successful `run` publishes one unit and returns the original
byte count.

The record becomes:

```text
active=1
actions=enabled
worker_runs=3
```

## Count requests; use the wait queue only to sleep

The wait queue does not remember arrivals, so store the pending count in an
`atomic_t`.
Sleep with `wait_event_interruptible()` using the exact condition
`pending > 0 || kthread_should_stop()`.
Call `wake_up_interruptible()` only after publishing a request.

Use `atomic_add_unless(..., INT_MAX)` when a command is accepted.
If it returns false, return `-EOVERFLOW` without changing the count or waking
the thread.

After waking, check `kthread_should_stop()` before consuming work.
Consume one unit with `atomic_dec_if_positive()` and record one `worker_runs`
increment.
It leaves an already-zero counter unchanged and returns a negative value.
Recheck the wait condition instead of polling or delaying.

The pending count needs atomic read-modify-write operations because proc writers
add requests while the thread consumes them.
The action switch is replaced as a whole, and one thread alone writes
`worker_runs`, so those fields retain their supplied `READ_ONCE()` and
`WRITE_ONCE()` operations.

Each `run` samples the action switch once.
Seeing disabled rejects the write; seeing enabled admits a unit that remains
valid even if another writer immediately disables future actions.
Concurrent successful writes must not merge: 100 successes must eventually add
exactly 100 runs.

## Lifecycle

Start the thread after state allocation and before procfs publication.
Failure point 4 supplies `ERR_PTR(-EINTR)` instead of creating the task.
Handle that value through the same `IS_ERR()` and `PTR_ERR()` path as a real
`kthread_run()` error.
On creation failure, destroy state.
On publication failure, stop and join the thread before destroying state.

Unload in this order:

1. remove the proc entry so no new commands begin
2. call `kthread_stop()` to wake, stop, and join the thread
3. destroy the state used by the thread

`kthread_stop()` waits until the thread function exits.
Pending units may be discarded once unload asks the thread to stop.

## Your task

Edit these four files:

- `Kbuild`: link `execution/worker.o`
- `core/main.c`: start, roll back, and stop the thread
- `interface/proc.c`: gate `run` and submit one request
- `execution/worker.c`: implement the request, wait, and lifecycle operations

The state counter and its operations are supplied.
Use supplied `RK_WORKER_NAME` when creating the task.

Run:

```sh
./rootkitlings check
```

The checker proves creation failure and rollback, idle sleep, command gating,
exact sequential and concurrent counts, clean unload, and repeated load cycles.

## References

- [Kernel threads][ref-1]
- [Stopping a kernel thread][ref-2]
- [Wait queues][ref-3]
- [Atomic operations][ref-4]

[ref-1]:
  https://docs.kernel.org/7.2/driver-api/basics.html#c.kthread_run

[ref-2]:
  https://docs.kernel.org/7.2/driver-api/basics.html#c.kthread_stop

[ref-3]:
  https://docs.kernel.org/7.2/driver-api/basics.html#wait-queues-and-wake-events

[ref-4]:
  https://docs.kernel.org/7.2/core-api/wrappers/atomic_t.html
