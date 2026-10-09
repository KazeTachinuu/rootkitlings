# Record authorized actions and rmdir results

This milestone joins the resident worker, one-use credential action, and selected `rmdir()` return probe in one module.
Those mechanisms are supplied.
Your new mechanism is a bounded event ring shared by two producers.

The control endpoint remains `/proc/rootkitlings` with mode `0622`.
It accepts exactly:

- `enable`
- `disable`
- `run`
- `root TOKEN`

The separate `/proc/rootkitlings_events` endpoint is root-readable with mode `0400`.
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

## Ring state

`instrumentation/events.c` owns an array of 16 complete records and four state fields:

- the index of the oldest retained record
- the retained count
- the next sequence number, initially 1
- the overwrite count, initially 0

Protect the array and all four fields with one ordinary `spinlock_t`.
An append to a non-full ring writes after the newest record.
An append to a full ring replaces the oldest record, advances the oldest index, and increments `overwritten`.
Allocate the sequence number while holding the same lock.

Both producers run outside hard-IRQ and softirq context on the pinned non-RT kernel.
Do not allocate, copy user memory, format output, unregister a probe, or call a sleeping function while holding the event lock.

## Typed producers

Expose two functions:

- `rk_events_record_credential_change()`
- `rk_events_record_rmdir(long result)`

Neither accepts a task ID.
The event component records `task_pid_nr(current)` at the append point.
The authorization path calls its typed function only after credential replacement succeeds.
Preparation failure, cancellation, rejected tokens, and replay append nothing.
The Kretprobe calls its typed function only for the exact selected path after the syscall returns.

## Reading a snapshot

Copy retained records and counters into a bounded local snapshot while holding the lock.
Release the lock before calling any `seq_*()` function.

Opening the events endpoint does not take a snapshot.
The first read at offset zero fills the seq-file buffer from one snapshot.
Partial reads drain that buffer.
EOF remains EOF when later events arrive.
Seeking back to offset zero makes the next read take a new snapshot.

## Lifetime

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

The supplied state, worker, scheduler, and credential implementation remain unchanged.
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

The checker covers every initialization rollback, both event producers, exact ring boundaries, concurrent appends, sequence allocation, overwrite accounting, seq-file snapshot behavior, unload, and reload reset.
The controlled authorization-start failure returns `-EINTR`; normal builds still call `wait_for_random_bytes()` and propagate its real result.

## Official Linux v7.2 references

- [Spinlock API](https://github.com/torvalds/linux/blob/v7.2/include/linux/spinlock.h)
- [Kernel locking guidance](https://github.com/torvalds/linux/blob/v7.2/Documentation/kernel-hacking/locking.rst)
- [Kretprobe contracts](https://docs.kernel.org/7.2/trace/kprobes.html)
- [`seq_file` implementation](https://github.com/torvalds/linux/blob/v7.2/fs/seq_file.c)
