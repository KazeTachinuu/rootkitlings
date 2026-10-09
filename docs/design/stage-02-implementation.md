# Stage 2 implementation slice

Status: incubation implementation complete and validated through M00; public route migration pending.
Nothing here changes the public course route yet.

## Implemented checkpoints

| Target | Incubator | Learner-owned change | Observable contract |
|---|---|---|---|
| L04 `proc-read-contract` | R05 `r05-proc-read` | Connect the proc entry to private state through one accessor | Exact state record, partial reads, EOF, rewind, rejected writes, safe held-descriptor unload |
| L05 `bounded-control-switch` | R06 `r06-control-switch` | Add one private Boolean gate and a strict procfs write boundary | Exact `enable`/`disable` commands, precise errors, state preservation, concurrent complete reads, permissions, unload and reset |
| L06 `timer-workqueue-scheduler` | R07 `r07-timer-workqueue` | Connect a preallocated timer to one non-reentrant work item | Bounded timer callback, gated worker runs, exact counters, timer and work teardown |
| L07 `event-driven-kernel-thread` | R08 `r08-event-thread` | Add counted requests, a wait queue and one resident thread | Exact accepted-request accounting, bounded saturation, idle sleep, stop and join |
| M00 `controlled-resident-core` | M00 `m00-controlled-resident-core` | Integrate the learned mechanisms into the Stage 1 module | Timer and proc producers share one capacity-one request slot and resident thread |

The four lessons are sparse overlays over the Stage 1 module.
They preserve all Stage 1 build-time failure points, rollback evidence, component ownership, and proc-before-state teardown.
M00 starts from the learner's passed R04 snapshot and asks the learner to reapply the Stage 2 mechanisms in one integrated module.

## Accepted control design

L04 keeps `struct rk_state` private and passes its borrowed pointer through `proc_create_single_data()`.
The show callback copies one immutable value through the state owner's accessor and lets `seq_file` implement byte counts, positions, EOF, and rewind.

L05 replaces the read-only helper with explicit `proc_ops` and `proc_create_data()`.
It accepts only `enable` and `disable`, with one optional final newline.
Bounds and the file offset are checked before `copy_from_user()`; the complete kernel-owned candidate is validated before one `WRITE_ONCE()` commit.
Every failure preserves the prior state.

The action switch is a `bool` read with `READ_ONCE()` and written with `WRITE_ONCE()`.
These operations force one compiler-visible scalar access; they do not provide an atomic read-modify-write operation or protect compound state.
Concurrent valid writers use last-writer-wins semantics, and readers must observe one complete old-or-new Boolean value.
The first genuine atomic read-modify-write operation appears in L07, where it bounds a counted pending-event queue at `INT_MAX` without coalescing successful requests.
The first real multi-field invariant introduces its mutex at one-use authorization.
The required route has no kernel-list consumer after unsupported module-list mutation was retired.

L06 uses a timer callback only to record a firing, submit one preallocated work item, and rearm the timer.
The normal workqueue callback owns the observable action and is the first sleep-capable execution boundary.
Unload removes procfs, shuts down the timer, cancels the work item synchronously, and only then destroys state.

L07 uses a counted atomic pending queue because concurrent successful `run` writes must not merge.
The wait queue only blocks the resident thread; it does not store work.
Stop takes priority over pending work, and `kthread_stop()` joins the task before state destruction.

M00 deliberately changes the manual lesson's counted queue to one capacity-one slot.
The periodic and manual producers call the same nonblocking request function, and the resident thread is the only action owner.
This keeps periodic demand bounded and removes the now-redundant workqueue from the integrated design.

## Runtime evidence

The R05 matrix builds every inherited failure variant and verifies exact reads, terminal short reads, repeated EOF, rewind, write rejection, two held-descriptor unloads, reload, and a clean kernel log.

The R06 matrix builds the same variants and checks all four accepted command forms; exact `EINVAL`, `E2BIG`, and `EFAULT` paths; bounds before user access; nonzero offsets; repeated writes on one descriptor; mode and ownership; unprivileged denial; concurrent old-or-new records; two held-descriptor unloads; reset on reload; and a clean kernel log.

The R07 matrix proves bounded timer callbacks, one-writer counters, disabled and enabled behavior, workqueue non-reentrance, synchronous timer and work shutdown, two load cycles, and a clean kernel log.

The R08 matrix proves exact sequential, burst, concurrent, and switch-race accounting; bounded saturation; an idle sleeping task; creation and publication rollback; held-descriptor unload; two load cycles; and a clean kernel log.

The M00 matrix proves that both producers share one capacity-one admission point, validates `EACCES` and `EAGAIN`, derives its completion bound from observed admissions and timer firings, exercises blocked and active unload, traces state cleanup on failures, checks two load cycles, and leaves no worker or kernel warning.

The three checks that trace state rollback share one allocation-tracing helper in `vm/checks/lib.sh`; the event lessons do not carry copied trace parsers.

## Public adoption boundary

Stage 2 remains private only because the target catalog, metadata, state migration, and cumulative snapshot path must be adopted together after the remaining stages are implemented.
There is no unfinished Stage 2 mechanism hidden behind that boundary.
