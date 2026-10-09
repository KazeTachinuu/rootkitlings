# Stage 3 cumulative architecture

Status: implemented and independently verified in M01.
It does not change the public route.

## Boundary

Stage 3 keeps two mechanism branches separate until the milestone:

```text
task attribution -> credential model -> credential replacement -> one-use authorization

bounded control -> managed Kprobe -> return-aware rmdir telemetry
```

M01 starts from the learner's passed M00 workspace and integrates those branches.
It does not ask the learner to discover another authorization scheme, probe ABI, or copy primitive.
The only new mechanism is a bounded event ring protected by a spinlock.

## Cumulative files

M01 keeps the M00 module and adds components only when they have a live role:

```text
Kbuild
include/rootkit.h
core/main.c
core/state.c
interface/proc.c
execution/worker.c
execution/scheduler.c
control/authorization.c
privilege/credentials.c
instrumentation/return_probe.c
instrumentation/events.c
```

There is no symbol-resolution component.
The Kretprobe core owns named target resolution through `.symbol_name`.
There is no generic crypto component: the authorization component owns its random binary token, exact hexadecimal boundary, comparison, rotation, and erasure.

## Learner work

The M01 overlay supplies one merged M00-based workspace.
The learner does not copy or reconcile separate lesson workspaces.

These implementations are supplied unchanged:

- `core/state.c`
- `execution/worker.c`
- `execution/scheduler.c`
- `privilege/credentials.c`
- the token parser and authorization transition
- the syscall-wrapper decoding and bounded no-fault copy

The learner edits only:

- `Kbuild`
- `include/rootkit.h`
- `core/main.c`
- `interface/proc.c`
- `control/authorization.c`
- `instrumentation/return_probe.c`
- `instrumentation/events.c`

The work is to:

1. link the Stage 3 components into the M00 module
2. place them in the cumulative init, rollback, and exit order
3. connect the protected `root TOKEN` command to the inherited authorization path
4. add one event call after a successful credential replacement
5. connect selected `rmdir()` returns to the same typed event API
6. implement the spinlocked ring, overwrite rule, and oldest-first snapshot

This makes integration visible without turning the milestone into a verbatim repeat of L11 and L13.
The event structs, fixed output renderer, and proc publication are supplied so the learner's new algorithm remains the ring itself.

## Procfs interfaces

`/proc/rootkitlings` remains the control and status endpoint.
It is root-readable and world-writable with mode `0622` because the token is bearer authorization and the writer may begin unprivileged.
It accepts only the inherited complete commands:

```text
enable
disable
run
root TOKEN
```

Its record retains the fields needed to verify the integrated mechanisms:

```text
active=1
actions=enabled|disabled
timer_fires=N
worker_runs=N
reader_tid=N
reader_comm="..."
uids=R E S FS
gids=R E S FS
cap_userns=initial|nested
cap_permitted=HHHHHHHHHHHHHHHH
cap_effective=HHHHHHHHHHHHHHHH
completed_actions=N
token=HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
```

`/proc/rootkitlings_events` is a separate root-readable `0400` observation endpoint.
Separating it prevents a variable-length event list from weakening the fixed control record.
Its record is:

```text
event_capacity=16
retained=N
overwritten=N
return_capacity=16
returns_missed=N
sequence=N kind=credential-change tid=N
sequence=N kind=rmdir tid=N result=success|failure
```

The event lines are oldest first.
The selected rmdir path remains the exact L13 path, so storing the same constant in every record would add no information.
`overwritten` counts records displaced by a full ring.
`return_capacity` and `returns_missed` describe the separate Kretprobe instance pool.
`returns_missed` never means a failed `rmdir()`.

Opening the events file does not take a snapshot.
The first read at offset zero copies the ring and fills the seq-file buffer.
Partial reads drain that buffered record, and reads after end of file remain at end of file even if new events arrive.
Seeking to offset zero resets the traversal; the next read takes a new snapshot.

## Ring invariant

The event component owns a fixed array of 16 complete records and four fields:

- index of the oldest retained record
- number of retained records, from 0 through 16
- next 64-bit sequence number, starting at 1
- 64-bit overwrite count, starting at 0

One ordinary `spinlock_t` protects the array and all four fields.
Neither producer is a softirq or hard-IRQ producer.
The proc writer runs in ordinary process context.
The Kretprobe return handler executes on behalf of the syscall task but remains a non-sleeping probe handler with preemption disabled on the pinned configuration.
The pinned non-RT kernel may therefore use `spin_lock()` because no interrupt-context path acquires this lock.
The checker verifies that `CONFIG_PREEMPT_RT` is disabled.
If a later component records from a softirq or hard-IRQ context, that checkpoint must change and explain the locking contract then.

Appending to a non-full ring writes after the newest retained record and increments the count.
Appending to a full ring replaces the oldest record, advances the oldest index by one, and increments `overwritten`.
Every append receives one sequence number while holding the lock, so concurrent writers cannot duplicate or reorder sequence allocation.

The show callback copies the complete retained set and counters to a bounded local snapshot while holding the lock, then releases the lock before any `seq_printf()`.
No allocation, user access, formatting, sleeping operation, or probe unregister runs inside the spinlocked section.

## Event admission

The inherited authorization algorithm remains unchanged except for one call to `rk_events_record_credential_change()` after `rk_credentials_replace_current()` returns success.
Wrong, malformed, disabled, stale, replayed, preparation-failed, and cancelled commands append no credential event.
The token is still consumed and rotated after preparation failure or cancellation, as established by L11.

The Kretprobe entry handler keeps the L13 admission rule and exact path match.
The matching return handler passes its result to `rk_events_record_rmdir()`.
The event owner appends one `rmdir/success` event for return zero and one `rmdir/failure` event for a negative return.
Unselected paths, malformed pointers, disabled calls, and missed return instances append no event.

The typed event functions accept no caller-supplied task ID.
The event owner snapshots `task_pid_nr(current)` at the recording point.
The value is attribution to the executing thread, not authentication and not a process-wide identifier.

## Resource order

M01 acquires reachable resources in this order:

```text
create state
reset event ring
start worker thread
start authorization state
register Kretprobe
publish passive events endpoint
publish active control endpoint
start timer
```

The static event ring is reset before either event source becomes reachable.
The timer starts last because it cannot fail and immediately exposes a producer.

Rollback and unload close sources before their consumers and borrowed state:

```text
remove active control endpoint
remove passive events endpoint
shut down the timer
unregister the Kretprobe
erase authorization state
stop and join the worker
destroy state
```

Publishing the passive endpoint first ensures that a partial publication never exposes commands before the complete interface exists.
Failure to publish the events endpoint leaves neither entry.
Failure to publish the control endpoint removes the events entry before returning the original error.
Missing Kretprobe registration leaves neither probe nor proc entry.
Credential preparation and pre-commit cancellation are runtime action failures, not module-init failures.

## Required evidence

The M01 checker must prove all of the following rather than accepting source shape as runtime evidence:

- every inherited M00 init failure still returns its exact errno and leaks no resource
- controlled authorization startup failure returns its injected errno, stops the
  worker, and leaves no token, probe, proc entry, timer, or state allocation
- the missing Kretprobe target returns `ENOENT` and leaves no registry entry
- both proc publication failures remove every published path and the registered probe
- timer expiry and `run` still share one capacity-one pending slot
- disabled timer callbacks increment `timer_fires` without completing worker work
- `run` preserves its `EACCES` and `EAGAIN` results, and only the worker increments
  `worker_runs`
- the same switch gates worker requests, credential changes, and probe admission
- all four commands preserve their exact grammar, optional single LF, offset, size,
  malformed-input, and state-preservation contracts
- disabled, malformed, wrong, stale, and replayed tokens preserve action and event state
- one current token raced by many writers produces exactly one credential event
- that credential event identifies the one thread whose credential transition succeeded
- credential preparation failure and cancellation consume the token but produce no event
- a successful action changes only its calling thread and produces one matching event
- exact selected rmdir success and failure each produce one correlated event
- near-match, unrelated, invalid-pointer, and unterminated paths produce none
- deterministic ring cases cover 0, 1, 15, 16, 17, and more than 32 admitted events
- mixed concurrent producers yield no duplicate sequences; retained sequences are
  contiguous and ascending; exactly the newest 16 remain without assuming kind order
- `overwritten` equals total admitted events minus 16 after capacity is exceeded
- opening an events descriptor does not snapshot; its first small read does; later
  events do not alter the buffered record, EOF remains stable, and rewind regenerates it
- unload during admitted work leaves no proc entry, worker, timer, Kretprobe, or warning
- immediate reload executes token generation, publishes a valid nonzero token, and
  starts with an empty ring, next sequence 1, and zero overwrite and action counters

Static checks must reject direct access to ring storage or indices outside `instrumentation/events.c`, sleeping or formatting inside the ring lock, raw probe-address use, alternate command paths, manual credential-field mutation, and unbounded or faulting user-path access in the probe.
They must accept harmless helpers, comments, formatting, and equivalent control flow.
Hostile fixtures cover macro-hidden forbidden calls and split formatting.

## Pinned references

- [Linux v7.2 spinlock API](https://github.com/torvalds/linux/blob/v7.2/include/linux/spinlock.h)
- [Linux v7.2 locking guidance](https://github.com/torvalds/linux/blob/v7.2/Documentation/kernel-hacking/locking.rst)
- [Linux v7.2 Kretprobe contracts](https://docs.kernel.org/7.2/trace/kprobes.html)
- [Linux v7.2 seq_file implementation](https://github.com/torvalds/linux/blob/v7.2/fs/seq_file.c)
