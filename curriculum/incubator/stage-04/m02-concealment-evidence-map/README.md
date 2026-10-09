# Omit the resident worker and map the affected view

This milestone adds one result to the existing module: while actions are enabled, the resident worker's numeric name is omitted from ordinary bounded `getdents64()` results for the root of a procfs mount.

The worker is not removed.
Direct `/proc/PID` lookup, its `comm` file, task execution, `/proc/modules`, and `/sys/module` remain unchanged.
The evidence endpoint states that scope directly; it does not claim that the worker is hidden or that a property has been independently verified.

The control endpoint remains `/proc/rootkitlings` with mode `0622`.
The event endpoint remains `/proc/rootkitlings_events` with mode `0400`.

## Process identity

After creating the worker, acquire its `PIDTYPE_TGID` identity with `get_task_pid()`.
The process-view component owns that `struct pid` reference.
It must not retain a borrowed `task_struct` pointer or a decimal PID string.

A numeric PID is meaningful only in a PID namespace.
For each selected proc root, obtain that mount's namespace with `proc_pid_ns()` and translate the owned identity with `pid_nr_ns()`.
If the result is zero, that worker is not visible from the mount and the result must remain unchanged.

Format the positive translated number into the return-probe instance.
Do not store a process-wide formatted target.

## Directory result

Reuse the supplied `rk_filter_dirents64()` helper.
It is the same validated packed-record filter used in L15.
Do not add another parser.

The supplied integration resolves the private `filldir64()` callback with a managed Kprobe and observes `iterate_dir()` with a Kretprobe.
The `iterate_dir()` entry receives the actual directory file, so a concurrent close or descriptor reuse cannot redirect the scope check to another file.
The callback identity limits the private cast to native `getdents64()` state.

That state layout belongs to Linux v7.2, not the stable module API.
Compile-time checks fix every field offset and the total size for the pinned x86-64 kernel.
Before filtering, the return handler also checks the remaining count, produced length, final-record length, and final-record boundary.
A disagreement leaves the result unchanged.

The process-view hook must:

- sample the action gate at entry
- select only a procfs root using the actual directory file
- accept only native `getdents64()` callback state
- accept at most 32 KiB of returned records
- translate the worker TGID through the selected proc mount
- copy user bytes without faulting
- remove only the exact translated PID name
- preserve retained directory cookies
- leave invalid, oversized, invisible-namespace, and all-match results unchanged
- return `-EFAULT` if writing a changed result back fails
- free temporary storage on every path

The gate is sampled once.
A concurrent enable or disable may therefore affect the current call or the next call.
A missed Kretprobe instance is a diagnostic count; it is not a count of missed omissions.

Copy-back is not transactional.
Another userspace thread can change, unmap, or protect its buffer between directory iteration and the return handler.
A copy-back fault becomes `-EFAULT`; bytes already written by either path cannot be rolled back.

## Evidence map

Publish `/proc/rootkitlings_evidence` as root-owned mode `0400` with exactly this record:

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

Translate `target_pid` through the PID namespace of the procfs instance used to read this endpoint.
The endpoint uses normal seq-file behavior: the first read at offset zero renders one record, partial reads drain that record, EOF stays EOF, and seeking to zero renders a new record.

## Lifetime

Acquire resources in this order:

1. create state
2. reset the event ring
3. start the worker
4. start authorization
5. register the `rmdir()` return probe
6. acquire the worker TGID and register the process-view hooks
7. publish the events endpoint
8. publish the evidence endpoint
9. publish the control endpoint
10. start the timer

Release them in this order:

1. remove the control endpoint
2. remove the evidence endpoint
3. remove the events endpoint
4. stop the timer
5. unregister the process-view hooks and release the TGID
6. unregister the `rmdir()` return probe
7. erase authorization state
8. stop and join the worker
9. destroy state

The process-view hook must be drained before releasing its PID reference.
The worker must remain alive until the process-view component has stopped.

## Files

Complete the milestone in:

- `execution/worker.c`
- `instrumentation/getdents_probe.c`
- `interface/evidence.c`
- `core/main.c`

The declarations, constants, proc operations, packed-record helper, and all other cumulative mechanisms are supplied.

Run:

```bash
./rootkitlings check
```

The checker covers both gate states, namespace translation, direct-view controls, packed-record boundaries and cookies, concurrency, every initialization rollback, unload, reload, and the complete M01 behavior.

## Official Linux v7.2 references

- [`struct pid` ownership and namespace translation](https://github.com/torvalds/linux/blob/v7.2/include/linux/pid.h)
- [procfs PID lookup and enumeration](https://github.com/torvalds/linux/blob/v7.2/fs/proc/base.c)
- [native `getdents64()` record construction](https://github.com/torvalds/linux/blob/v7.2/fs/readdir.c)
- [procfs entry teardown](https://github.com/torvalds/linux/blob/v7.2/fs/proc/inode.c)
- [Kprobe unregister synchronization](https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c)
