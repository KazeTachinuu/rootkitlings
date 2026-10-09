# Authorize the operator and record activity

The foothold from M00 accepts local commands, but any process with write access
could use them.
Give the operator a one-use secret for the privileged action, then keep a short
activity log showing what actually happened.

The mission combines three jobs:

- accept one authorized credential change;
- observe whether one selected `rmdir()` call succeeded or failed;
- retain the newest 16 events in order.

The log uses fixed storage because observation code must not stop to allocate
memory.
When the log is full, the next event replaces the oldest one.

The control endpoint remains `/proc/rootkitlings` with mode `0622`.
It accepts exactly:

- `enable`
- `disable`
- `run`
- `root TOKEN`

The separate `/proc/rootkitlings_events` endpoint is root-readable with mode
`0400`.
It begins with:

```text
event_capacity=16
retained=N
overwritten=N
return_capacity=16
returns_missed=N
```

Event records follow oldest first:

```text
sequence=N kind=credential-change tid=N
sequence=N kind=rmdir tid=N result=success
sequence=N kind=rmdir tid=N result=failure
```

## Keep the newest events

`instrumentation/events.c` owns 16 complete records and four fields:

- the index of the oldest retained record
- the retained count
- the next sequence number, initially 1
- the overwrite count, initially 0

Protect everything with one ordinary `spinlock_t`.
Append after the newest record, or replace and advance the oldest record while
incrementing `overwritten` when full.
Allocate sequence numbers under the same lock.

Both producers run outside hard-IRQ and softirq context on this pinned non-RT
kernel.
Under the lock, never allocate, copy user memory, format, unregister probes, or
sleep.

## Record two event types

Expose two functions:

- `rk_events_record_credential_change()`
- `rk_events_record_rmdir(long result)`

Neither accepts a task ID; record `task_pid_nr(current)` at append.
Record credentials only after successful replacement; failures, cancellation,
rejected tokens, and replay append nothing.
Record `rmdir()` only for the exact selected path after return.

## Read one stable snapshot

Copy records and counters into a bounded snapshot under the lock, then unlock
before any `seq_*()` call.

Opening does not snapshot.
The first offset-zero read snapshots; partial reads drain it, EOF persists, and
seeking to zero requests a new snapshot.

## Start and stop safely

Acquire resources in this order:

1. create state
2. reset the event ring
3. start the worker
4. start authorization
5. register the Kretprobe
6. publish the events endpoint
7. publish the control endpoint
8. start the timer

Unload in this order:

1. remove the control endpoint
2. remove the events endpoint
3. stop the timer
4. unregister the Kretprobe
5. erase authorization state
6. stop and join the worker
7. destroy state

## Files

The supplied state, worker, scheduler, and credential implementation remain
unchanged.
Complete the integration and event ring in:

- `Kbuild`
- `include/rootkit.h`
- `core/main.c`
- `interface/proc.c`
- `control/authorization.c`
- `instrumentation/return_probe.c`
- `instrumentation/events.c`

Run:

```bash
./rootkitlings check
```

The checker covers rollback, both producers, ring boundaries, concurrency,
sequence/overwrite accounting, snapshots, unload, and reset.
The controlled authorization-start failure returns `-EINTR`; normal builds still
call `wait_for_random_bytes()` and propagate its real result.

## Official Linux v7.2 references

- Spinlock API
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/spinlock.h>
- Kernel locking guidance
  <https://github.com/torvalds/linux/blob/v7.2/Documentation/kernel-hacking/locking.rst>
- Kretprobe contracts
  <https://docs.kernel.org/7.2/trace/kprobes.html>
- `seq_file` implementation
  <https://github.com/torvalds/linux/blob/v7.2/fs/seq_file.c>
