# Hide one process listing and map the blind spot

An operator wants the resident worker omitted from an ordinary listing of
`/proc`.
A defender wants to know exactly which view was altered and which views still
tell the truth.

This project does both.
While actions are enabled, one filtered directory listing omits the worker's
numeric PID.
The process still runs and remains visible through direct `/proc/PID` access,
its `comm` file, scheduler state, `/proc/modules`, and `/sys/module`.

The second endpoint is an evidence map: a precise statement of the blind spot,
not a claim that the module is invisible.

## Identify the worker in each PID namespace

After creating the worker, acquire its `PIDTYPE_TGID` identity with
`get_task_pid()`.
Own that `struct pid` reference rather than a borrowed task pointer or decimal
PID.
For each selected proc root, use `proc_pid_ns()` and `pid_nr_ns()` to translate
the identity into that mount's namespace.
Leave the result unchanged when translation returns zero.

Reuse the supplied `rk_filter_dirents64()` helper.
The module observes the kernel function that fills a directory result and its
return value.
The implementation uses a Kprobe and Kretprobe for those two observation points.
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

## Describe exactly what changed

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

## Start and stop safely

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
