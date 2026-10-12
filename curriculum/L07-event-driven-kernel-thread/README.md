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

## Wait queue and atomic counter

A **wait queue** lets a thread sleep until a condition becomes true.
It does not remember past wake-ups, so store the pending count in an
`atomic_t` (an integer safe for concurrent read-modify-write).

```text
proc write "run"                 kernel thread
      |                               |
  atomic_add(pending)          wait_event_interruptible
      |                          (pending > 0 || should_stop)
  wake_up_interruptible              |
                               atomic_dec_if_positive
                                     |
                               worker_runs++
```

- `atomic_add_unless(..., INT_MAX)`: increment pending unless already at max
  (return `-EOVERFLOW` if false).
- `wait_event_interruptible()`: sleep until `pending > 0 || kthread_should_stop()`.
- `atomic_dec_if_positive()`: consume one unit (returns negative if already zero).

Each `run` samples the action switch once. Disabled rejects the write.
Concurrent writes must not merge: 100 successes produce exactly 100 runs.

## Lifecycle

Start the thread after state allocation, before procfs publication.
`kthread_run()` returns `ERR_PTR` on failure; handle with `IS_ERR()`.

Unload in reverse:

1. remove proc entry
2. `kthread_stop()` (wakes, stops, and joins the thread)
3. destroy state

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
./rklings check
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
