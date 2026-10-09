# Combine the timer, controls, and worker

The earlier lessons built state, a control file, a timer, and a kernel thread in
isolation.
Now run them together as one resident module.

The operator can enable or disable actions and request work through
`/proc/rootkitlings`.
A timer can request the same work automatically.
Only one worker performs it, so the two request paths cannot race through the
action.

The two request paths meet at one worker:

```text
timer expiry ----\
                  -> rk_worker_request() -> one pending slot -> kernel thread
procfs `run` -----/                                     |
                                                         -> worker_runs
```

The action is deliberately harmless: increment a counter.
The goal is to make control, background execution, and unload behave as one
system.

## Operator controls

`/proc/rootkitlings` accepts these complete commands, with or without one
trailing newline:

- `enable`
- `disable`
- `run`

Its complete status record is:

```text
active=1
actions=enabled
timer_fires=12
worker_runs=7
```

`timer_fires` counts every callback; `worker_runs` counts thread-completed
actions and is the complete observable action here.

## Keep one job waiting

Keep one private capacity-one `atomic_t pending` in `execution/worker.c`:

- `0` means empty
- `1` means one request is waiting

`rk_worker_request()` returns `-EACCES` when disabled.
Otherwise claim the slot with `atomic_cmpxchg(&pending, 0, 1)`, return `-EAGAIN`
if full, and wake the queue only after publishing `1`.

The timer ignores either error and drops a request when full.
Procfs returns the error; successful `run` means queued, not completed.

## Run jobs in one thread

The thread waits until `pending` is nonzero or `kthread_should_stop()` is true.
After waking:

1. check `kthread_should_stop()` first
2. clear the slot with `atomic_xchg(&pending, 0)`
3. read the action switch again
4. increment `worker_runs` only if that read sees enabled

The second gate read resolves a racing disable.
The worker discards the request if disable wins, but may finish after the write
returns if it observed enabled first.
Disable gates decisions; it does not wait for prior work.

Use `READ_ONCE()` and `WRITE_ONCE()` for the whole-value gate and single-writer
counters.
The producer/consumer slot requires atomic read-modify-write operations.

## Start and stop safely

Create resources in this order:

1. create state
2. start the worker
3. publish procfs
4. start the timer

The worker must precede both request sources.
Failure point 4 returns `ERR_PTR(-EINTR)` instead of a worker; publication
failure must stop and join a created worker before state destruction.

Unload in this order:

1. remove procfs
2. call `timer_shutdown_sync()`
3. call `kthread_stop()`
4. destroy state

Proc removal closes manual requests, `timer_shutdown_sync()` closes periodic
requests and rearming, and `kthread_stop()` wakes and joins the consumer.
Keep state valid until all are unreachable.

## Files

Complete the integration in:

- `Kbuild`
- `core/main.c`
- `core/state.c`
- `include/rootkit.h`
- `interface/proc.c`
- `execution/worker.c`
- `execution/scheduler.c`

The function names, task name, timer interval, command grammar, and failure
point are supplied.
Do not add allocation, delays, logging, context predicates, a workqueue, or
another request path.

Run:

```bash
./rootkitlings check
```

The checker covers request races and gates, status, rollback, held descriptors,
unload, reload, reset, and kernel warnings.

## Official Linux v7.2 references

- Kernel threads
  <https://docs.kernel.org/7.2/driver-api/basics.html#c.kthread_run>
- Stopping a kernel thread
  <https://docs.kernel.org/7.2/driver-api/basics.html#c.kthread_stop>
- Wait queues
<https://docs.kernel.org/7.2/driver-api/basics.html#wait-queues-and-wake-events>
- Kernel timers
  <https://docs.kernel.org/7.2/driver-api/basics.html#timers>
- Atomic operations
  <https://docs.kernel.org/7.2/core-api/wrappers/atomic_t.html>
- `READ_ONCE()` and `WRITE_ONCE()`
  <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/memory-barriers.txt?h=v7.2>
- `timer_shutdown_sync()`
  <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/kernel/time/timer.c?h=v7.2>
