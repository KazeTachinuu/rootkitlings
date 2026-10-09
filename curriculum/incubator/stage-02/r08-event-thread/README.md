# Run work in a kernel thread

Add a `run` command to `/proc/rootkitlings`.
The command submits one unit of work to one kernel thread.
The thread sleeps when there is no work and records one completed run for each accepted command.

The complete write grammar is:

- `enable`
- `enable\n`
- `disable`
- `disable\n`
- `run`
- `run\n`

`run` returns `-EACCES` while actions are disabled.
Once the action switch is enabled, every successful `run` adds exactly one pending unit and returns its full byte count.

The proc record becomes:

```text
active=1
actions=enabled
worker_runs=3
```

## Keep the request count separate from the wait queue

A wait queue provides a place for the thread to sleep.
It does not remember how many commands arrived.
Store that count in an `atomic_t` and use the wait queue only to block until either:

- the pending count is greater than zero
- the thread has been asked to stop

Use `atomic_add_unless(..., INT_MAX)` when a command is accepted.
This adds one without wrapping the counter.
Wake the thread only after the pending unit has been published.

After waking, check `kthread_should_stop()` before consuming work.
Consume one unit with `atomic_dec_if_positive()` and record one `worker_runs` increment.
Then evaluate the wait condition again.
Do not poll or add a delay.

The pending count needs atomic read-modify-write operations because several proc write callbacks may add requests while the thread consumes them.
The action switch does not: it is one value replaced as a whole, so its getter and setter use `READ_ONCE()` and `WRITE_ONCE()`.
`worker_runs` also uses `READ_ONCE()` and `WRITE_ONCE()` because the one kernel thread is its only writer; procfs only reads it.
Use atomic RMW operations for shared updates that can race, not merely because a value is visible to more than one execution context.

## Define concurrent writes precisely

Each `run` reads the action switch once:

- if that read sees disabled, the write returns `-EACCES` and submits nothing
- if that read sees enabled, the accepted unit remains valid even if another
  writer disables actions immediately afterward

Concurrent successful `run` writes must not merge.
If 100 writes succeed, the thread eventually records exactly 100 additional runs.

## Start and stop the thread

Start the thread after state allocation and before procfs publication.
If thread creation fails, destroy the state.
If procfs publication fails, stop and join the thread before destroying the state.

Failure point 4 supplies `ERR_PTR(-EINTR)` instead of creating the task.
Handle that value through the same `IS_ERR()` and `PTR_ERR()` path as a real `kthread_run()` error.

Unload in this order:

1. remove the proc entry so no new commands begin
2. call `kthread_stop()` to wake, stop, and join the thread
3. destroy the state used by the thread

Pending units may be discarded once unload asks the thread to stop.
Units that completed before that point remain counted.

## Your task

Edit these four files:

- `Kbuild`
- `core/main.c`
- `interface/proc.c`
- `execution/worker.c`

The state counter and its narrow operations are already supplied so this exercise stays focused on the thread lifecycle and request path.

Run:

```bash
./rootkitlings check
```

The checker covers creation failure, rollback, an idle sleeping thread, disabled commands, exact sequential and concurrent request counts, unload, and two complete load cycles.

## Official Linux v7.2 references

- [Kernel threads](https://docs.kernel.org/7.2/driver-api/basics.html#c.kthread_run)
- [Stopping a kernel thread](https://docs.kernel.org/7.2/driver-api/basics.html#c.kthread_stop)
- [Wait queues](https://docs.kernel.org/7.2/driver-api/basics.html#wait-queues-and-wake-events)
- [Atomic operations](https://docs.kernel.org/7.2/core-api/wrappers/atomic_t.html)
