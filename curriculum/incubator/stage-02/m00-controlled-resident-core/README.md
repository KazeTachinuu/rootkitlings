# Unify timed and requested work

Build one resident execution path with two request sources:

```text
timer expiry ----\
                  -> rk_worker_request() -> one pending slot -> kernel thread
procfs `run` -----/                                     |
                                                         -> worker_runs
```

The timer and `run` command must not perform the action themselves.
They call the same nonblocking request function.
One kernel thread owns action execution.

This milestone starts from your passed procfs publication module from Stage 1.
Extend that module with the status fields, command, request sources, thread, and
teardown described below.

## Interface

`/proc/rootkitlings` accepts these complete commands, with or without one trailing newline:

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

`timer_fires` counts timer callbacks whether actions are enabled or disabled.
`worker_runs` counts actions completed by the kernel thread.
One `worker_runs` increment is the complete observable action in this milestone.

## One pending request

Keep one private `atomic_t pending` in `execution/worker.c`.
It is a capacity-one slot, not a counter:

- `0` means empty
- `1` means one request is waiting

`rk_worker_request()` first reads the action switch.
It returns `-EACCES` if actions are disabled.
When enabled, claim an empty slot with `atomic_cmpxchg(&pending, 0, 1)`.
Return `-EAGAIN` if the slot is already full.
After publishing `1`, wake the wait queue.

The timer ignores either error.
A full periodic request is dropped; it does not replace the request already waiting.
The proc callback returns the request error to its caller.
A successful `run` means that its request entered the slot, not that the action has already completed.

## Consume in one thread

The thread waits until `pending` is nonzero or `kthread_should_stop()` is true.
After waking:

1. check `kthread_should_stop()` first
2. clear the slot with `atomic_xchg(&pending, 0)`
3. read the action switch again
4. increment `worker_runs` only if that read sees enabled

The second action-switch read handles a disable that races with an admitted request.
If disable wins that read, the worker discards the request.
If the worker sees enabled first, it may increment `worker_runs` after the disable write returns.
Disable is therefore a gate for later decisions, not a synchronous wait for earlier work.

`actions_enabled` uses `READ_ONCE()` and `WRITE_ONCE()` because it is one whole-value gate.
`timer_fires` and `worker_runs` also use `READ_ONCE()` and `WRITE_ONCE()` because each has exactly one writer.
The pending slot needs atomic read-modify-write operations because request producers race with its consumer.

## Start and stop ownership

Create resources in this order:

1. create state
2. start the worker
3. publish procfs
4. start the timer

The worker must exist before either request source becomes reachable.
Failure point 4 supplies `ERR_PTR(-EINTR)` instead of creating the worker.
If procfs publication fails, stop and join the worker before destroying state.
Starting the preallocated timer cannot fail.

Unload in this order:

1. remove procfs
2. call `timer_shutdown_sync()`
3. call `kthread_stop()`
4. destroy state

Removing procfs closes the manual source.
`timer_shutdown_sync()` closes the periodic source and prevents rearming.
`kthread_stop()` wakes and joins the only consumer.
The state remains valid until all three are unreachable.

## Files

Complete the integration in:

- `Kbuild`
- `core/main.c`
- `core/state.c`
- `include/rootkit.h`
- `interface/proc.c`
- `execution/worker.c`
- `execution/scheduler.c`

The function names, task name, timer interval, command grammar, and failure point are supplied.
Do not add allocation, delays, logging, context predicates, a workqueue, or another request path.

Run:

```bash
./rootkitlings check
```

The checker covers all failure points, publication rollback, disabled and enabled timer behavior, manual and concurrent requests, both unload states, a held proc descriptor, two load cycles, exact status shape, reset state, and kernel warnings.

## Official Linux v7.2 references

- [Kernel threads](https://docs.kernel.org/7.2/driver-api/basics.html#c.kthread_run)
- [Stopping a kernel thread](https://docs.kernel.org/7.2/driver-api/basics.html#c.kthread_stop)
- [Wait queues](https://docs.kernel.org/7.2/driver-api/basics.html#wait-queues-and-wake-events)
- [Kernel timers](https://docs.kernel.org/7.2/driver-api/basics.html#timers)
- [Atomic operations](https://docs.kernel.org/7.2/core-api/wrappers/atomic_t.html)
- [`READ_ONCE()` and `WRITE_ONCE()`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/memory-barriers.txt?h=v7.2)
- [`timer_shutdown_sync()`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/kernel/time/timer.c?h=v7.2)
