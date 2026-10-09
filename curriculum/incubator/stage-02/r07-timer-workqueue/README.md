# Defer timer work to a worker

The action switch now controls a periodic module action.
A kernel timer provides the timing, but its callback cannot sleep.
The timer therefore does only three bounded operations:

- record one timer firing
- queue preallocated work
- rearm the timer

The work callback runs on the normal system workqueue.
It reads the action switch and records one completed action only while the switch is enabled.
No memory is allocated in either callback.
For this lesson, the action is the counter increment: it makes worker execution observable without adding another mechanism.

The proc record becomes:

```text
active=1
actions=disabled
timer_fires=12
actions_completed=0
```

`timer_fires` keeps increasing while the module is loaded.
`actions_completed` increases only after `enable` has been written.
The existing write grammar and error behavior do not change.
The counters are not required to match: scheduling the same pending work item again may coalesce timer firings instead of building a backlog.

## Keep scheduling separate from state

Add `execution/scheduler.c` for the timer and workqueue objects.
Keep the counters private in `struct rk_state` as `atomic_long_t` values, with narrow read and increment operations in `rootkit.h`.

The timer and worker can update counters while procfs reads them.
Atomic operations make each individual counter access complete without introducing a lock or a multi-field invariant.

Start the scheduler only after procfs publication succeeds.
If an earlier initialization step fails, there is no timer or work item to undo.

## Stop every callback before freeing state

Unload in this order:

1. remove the proc entry so no new reads or writes begin
2. call `timer_shutdown_sync()` so the timer callback has finished and cannot rearm the timer
3. call `cancel_work_sync()` so queued or running work has finished
4. destroy the state used by both callbacks

`timer_delete_sync()` alone is not enough for a timer that rearms itself.
`timer_shutdown_sync()` also rejects later attempts to rearm it.

## Your task

Edit these six files:

- `Kbuild`
- `include/rootkit.h`
- `core/main.c`
- `core/state.c`
- `interface/proc.c`
- `execution/scheduler.c`

Run:

```bash
./rootkitlings check
```

The checker verifies the timer-to-worker path, disabled and enabled behavior, inherited procfs writes, failure rollback, teardown, and two complete load cycles.

## Official Linux v7.2 references

- [Kernel timers](https://docs.kernel.org/7.2/driver-api/basics.html#timers)
- [Workqueues](https://docs.kernel.org/7.2/core-api/workqueue.html)
- [Concurrency and contexts](https://docs.kernel.org/7.2/kernel-hacking/hacking.html)
- [`timer_shutdown_sync()` implementation](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/kernel/time/timer.c?h=v7.2)
- [Timer API declarations](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/linux/timer.h?h=v7.2)
