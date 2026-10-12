# Drive a workqueue from a kernel timer

The module needs a periodic heartbeat, but a timer callback cannot do work that
may sleep.
Make the timer signal a normal kernel worker and keep the timer itself short.
Success proves that scheduled activity reaches the right execution context and
can still be stopped cleanly.

A **workqueue** runs deferred functions in kernel worker threads, where
sleep-capable APIs may be used.
Build this path:

```text
timer expiry
  -> record timer_fires
  -> schedule_work(&action_work)
       -> read actions_enabled once
       -> if enabled, increment worker_runs
```

The counter increment is the complete observable work in this exercise; it does
not need to sleep.
The mechanism being added is the boundary between the timer callback and work
that may later need worker-thread context.

The proc record becomes:

```text
active=1
actions=disabled
timer_fires=12
worker_runs=0
```

## Timer and work behavior

A kernel timer is one-shot.
`jiffies` is a kernel counter that increments on every timer tick;
`msecs_to_jiffies(ms)` converts a millisecond interval to the corresponding
number of ticks.
The timer callback makes it periodic by calling `mod_timer()` with the current
`jiffies` plus the converted interval.
It performs only three bounded operations: record the firing, call
`schedule_work()`, and rearm the timer.

The work item is preallocated.
If that same item is already pending, another `schedule_work()` may be coalesced
rather than creating a backlog.
`timer_fires` and `worker_runs` therefore do not have to match.

The worker reads the action switch once.
An enabled read admits that run; a disabled read rejects it.
Writing `disable` prevents later admissions, but it is not a quiescence barrier:
a run admitted before the write may finish after the write returns.

Each counter also has exactly one writer:

- the timer callback writes `timer_fires`
- the one non-reentrant `action_work` item writes `worker_runs`

Procfs reads both counters concurrently.
The supplied state code therefore uses `READ_ONCE()` for each read and one
`READ_ONCE()` plus `WRITE_ONCE()` for each increment.
That read-plus-write is safe only under the single-writer rule; it is not an
atomic read-modify-write operation.
Adding another writer would require an atomic read-modify-write operation or a
lock.

## Ownership and shutdown

`execution/scheduler.c` owns one timer and one work item.
It receives a pointer to state owned by `core/main.c`; the scheduler neither
allocates nor frees that state.
Both callback objects are initialized before the timer is armed, so scheduler
start has no failure path.

Start the scheduler only after procfs publication succeeds.
Unload in this order:

1. remove procfs so no new control operations begin
2. call `timer_shutdown_sync()` to stop the producer and prevent rearming
3. call `cancel_work_sync()` to drain the consumer
4. destroy the state used by both callbacks

The borrowed pointer remains valid until both callback sources have stopped.
`timer_delete_sync()` is insufficient here because this timer rearms itself.

## Your task

- `Kbuild`: link `execution/scheduler.o`
- `core/main.c`: start after publication and stop before state destruction
- `execution/scheduler.c`: implement both callbacks and their lifecycle

Run:

```sh
./rklings check
```

The checker verifies the callback call boundaries, observable timer and worker
behavior, inherited failure rollback and procfs writes, two load cycles, and a
bounded post-unload clean period.

## References

- [Kernel timers](https://docs.kernel.org/7.2/driver-api/basics.html#timers)
- [Workqueues](https://docs.kernel.org/7.2/core-api/workqueue.html)
- [Concurrency and contexts][ref-1]
- [`READ_ONCE()` and `WRITE_ONCE()`][ref-2]
- [`timer_shutdown_sync()` implementation][ref-3]
- [Timer API declarations][ref-4]

[ref-1]:
  https://docs.kernel.org/7.2/kernel-hacking/hacking.html

[ref-2]:
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/memory-barriers.txt?h=v7.2

[ref-3]:
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/kernel/time/timer.c?h=v7.2

[ref-4]:
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/linux/timer.h?h=v7.2
