# Stage 2 implementation slice

Status: control boundary complete through target L05; resident execution and M00 remain pending.
Nothing here changes the public course route yet.

## Implemented checkpoints

| Target | Incubator | Learner-owned change | Observable contract |
|---|---|---|---|
| L04 `proc-read-contract` | R05 `r05-proc-read` | Connect the proc entry to private state through one accessor | Exact state record, partial reads, EOF, rewind, rejected writes, safe held-descriptor unload |
| L05 `bounded-control-switch` | R06 `r06-control-switch` | Add one private Boolean gate and a strict procfs write boundary | Exact `enable`/`disable` commands, precise errors, state preservation, concurrent complete reads, permissions, unload and reset |

Both checkpoints are sparse overlays over the Stage 1 module.
They preserve all Stage 1 build-time failure points, rollback evidence, component ownership, and proc-before-state teardown.

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
The first genuine atomic read-modify-write operation appears in L07, where it bounds pending event admission to one request.
The first real multi-field invariant will introduce its mutex at one-use authorization, and the first real list consumer will introduce kernel list mechanics at module-view suppression.

## Runtime evidence

The R05 matrix builds every inherited failure variant and verifies exact reads, terminal short reads, repeated EOF, rewind, write rejection, two held-descriptor unloads, reload, and a clean kernel log.

The R06 matrix builds the same variants and checks all four accepted command forms; exact `EINVAL`, `E2BIG`, and `EFAULT` paths; bounds before user access; nonzero offsets; repeated writes on one descriptor; mode and ownership; unprivileged denial; concurrent old-or-new records; two held-descriptor unloads; reset on reload; and a clean kernel log.

## Remaining Stage 2 work

- L06 `timer-workqueue-scheduler`: keep the timer callback bounded, use one writer per `unsigned long` counter, defer sleep-capable work to a worker, and close both lifetimes on unload.
- L07 `event-driven-kernel-thread`: admit at most one pending event with an atomic read-modify-write operation, then wake, stop, and join a resident worker without polling.
- M00 `controlled-resident-core`: unify the timer and proc producers through one capacity-one request path consumed by the resident thread, without retaining a redundant workqueue.

M00 starts from the learner's passed R04 cumulative snapshot.
L05-L07 are separate practice workspaces: their completion proves the prerequisite knowledge, but the learner applies those mechanisms again in M00.
No automatic workspace merge or additional engine feature is implied.

The target route remains private until these assets, metadata, state migration, and cumulative snapshot checks can be adopted atomically.
