# Omit the resident worker and map the affected view

While actions are enabled, omit the resident worker's numeric name from bounded
`getdents64()` results for a procfs root.
The worker still runs, and direct `/proc/PID`, `comm`, `/proc/modules`, and
`/sys/module` views remain unchanged.
The evidence endpoint declares this narrow scope without claiming invisibility
or independent verification.

## Identity and directory result

After creating the worker, acquire its `PIDTYPE_TGID` identity with
`get_task_pid()`.
Own that `struct pid` reference rather than a borrowed task pointer or decimal
PID.
For each selected proc root, use `proc_pid_ns()` and `pid_nr_ns()` to translate
the identity into that mount's namespace.
Leave the result unchanged when translation returns zero.

Reuse the supplied `rk_filter_dirents64()` helper.
The integration resolves private `filldir64()` state with a managed Kprobe and
observes `iterate_dir()` with a Kretprobe.
Use the actual entry-time directory file so close or descriptor reuse cannot
redirect the scope check.
Pinned Linux 7.2 x86-64 layout checks bound the private cast.

The hook must:

- sample the action gate once at entry;
- select only a procfs root and native `getdents64()` callback state;
- accept at most 32 KiB of returned records;
- validate remaining count, produced length, and final-record boundaries;
- translate and remove only the exact worker PID name;
- preserve retained directory cookies;
- leave invalid, oversized, all-match, or invisible-namespace results intact;
- return `-EFAULT` when changed bytes cannot be copied back;
- free temporary storage on every path.

Copy-back is not transactional because userspace may alter its buffer between
iteration and the return handler.
A fault cannot roll back bytes already written by either path.
A missed Kretprobe instance is diagnostic, not a missed-omission count.

## Evidence endpoint

Publish `/proc/rootkitlings_evidence` as root-owned mode `0400` with exactly:

```text
evidence_map_version=1
map_role=declared-filter-scope
target_kind=resident-worker-pid
target_pid=N
filter_gate=actions-enabled-at-iterate-dir-entry
filter_view=proc-root-getdents64-result
filter_match=exact-target-pid-name
filter_nonmatch=retain
outside_filter_scope=proc-pid-direct-lookup
outside_filter_scope=proc-pid-comm
outside_filter_scope=scheduler-task-execution
outside_filter_scope=proc-modules-content
outside_filter_scope=sys-module-tree
outside_filter_scope=non-proc-directories
filter_kernel_abi=linux-7.2-x86_64
filter_chunk_capacity=32768
filter_oversize=leave-unchanged
filter_all_match=leave-unchanged
filter_return_capacity=16
filter_returns_missed=N
```

Translate `target_pid` through the namespace of the procfs instance serving the
read.
The first offset-zero read snapshots one record; partial reads drain it, EOF
persists, and seeking to zero requests a new snapshot.

## Integration and lifetime

Complete `Kbuild`, `include/rootkit.h`, `execution/worker.c`,
`instrumentation/getdents_probe.c`, `interface/evidence.c`, and `core/main.c`.
The proc operations, record filter, and inherited mechanisms are supplied.

Acquire state, events, worker, authorization, `rmdir()` probe, process view,
events endpoint, evidence endpoint, control endpoint, then timer.
Release control, evidence, events, timer, process view and its TGID, `rmdir()`
probe, authorization, worker, then state.
Drain process-view hooks before releasing their PID reference, and keep the
worker alive until that component stops.

Run `./rootkitlings check`.
The checker exercises gate and namespace behavior, direct-view controls,
record/cookie boundaries, concurrency, rollback, unload, and inherited features.

## Official Linux v7.2 references

- `struct pid` ownership and namespace translation
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/pid.h>
- procfs PID lookup and enumeration
  <https://github.com/torvalds/linux/blob/v7.2/fs/proc/base.c>
- native `getdents64()` record construction
  <https://github.com/torvalds/linux/blob/v7.2/fs/readdir.c>
- procfs entry teardown
  <https://github.com/torvalds/linux/blob/v7.2/fs/proc/inode.c>
- Kprobe unregister synchronization
  <https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c>
