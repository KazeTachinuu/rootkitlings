# Manage two named Kprobes

Observe without altering two filesystem functions:

- `vfs_mkdir` when a directory is created
- `filename_rmdir` when a directory is removed

Each probe owns a seen flag that starts false on every load.
While actions are enabled, creation sets only `vfs_mkdir_seen` and removal sets
`filename_rmdir_seen`.
Both operations must still complete normally.

```text
active=1
actions=enabled
vfs_mkdir_seen=1
filename_rmdir_seen=0
```

## Work against the pinned kernel

The course uses Linux v7.2 on x86-64 with Kprobes, kallsyms, and debugfs.
In this build, `vfs_mkdir` is present and exported to modules; `filename_rmdir`
is present but not exported.
A module cannot link to the internal symbol, but the Kprobe core can resolve a
`.symbol_name` in the running kernel and install a probe when the target exists
and is probeable.
This is a pinned-build mechanism, not a stable API.

Registration fills each managed `struct kprobe.addr`.
Do not copy, call through, log, or publish that address.

## Keep handlers bounded

A pre-handler may run concurrently on several CPUs with preemption or interrupts
disabled.
Each handler must only read the action switch once, set its own flag to `true`
when enabled, and return zero so the target continues normally.
Do not allocate, access userspace, log, wait, take a sleeping lock, perform I/O,
or inspect or modify registers.

The supplied flags use `READ_ONCE()` and `WRITE_ONCE()`.
Concurrent stores are safe because every writer stores the same final value and
no update depends on old state.
A counter would require an atomic read-modify-write operation or protected
update.

Disabling actions does not unregister probes.
A handler admitted by an enabled read may finish after a concurrent `disable`.

## Own the complete probe set

Pass an array of probe pointers to `register_kprobes()`.
It registers in order and, if one fails, unregisters every earlier element
before returning the exact error.
Do not unregister an earlier probe again on that path.

Failure point 4 assigns the second probe a nonexistent name.
Lookup returns `-ENOENT`, and batch registration rolls back the first probe.

Normal initialization allocates state, registers both probes, then publishes
procfs.
If publication fails, unpublish nothing, unregister both probes, and free state.
Unload removes procfs, calls `unregister_kprobes()`, then frees state.
Unregistration waits until handlers can no longer use the borrowed state.

`/sys/kernel/debug/kprobes/list` provides independent evidence.
Addresses and tags such as `[FTRACE]` or `[OPTIMIZED]` are kernel-selected; only
probe type and exact symbol name are contractual.

## Your task

Edit only `instrumentation/probes.c`.
Complete both pre-handlers, batch registration, and batch unregistration.

Run:

```bash
./rootkitlings check
```

The checker pins the ABI assumptions and proves gated observations without
changing filesystem results.
It also verifies missing-symbol and procfs rollback, registry cleanup, unload,
and repeated load cycles.

## Official Linux v7.2 references

- [Kprobes API, handler constraints, and debugfs registry][ref-1]
- [`register_kprobes()` rollback and named lookup][ref-2]
- [`vfs_mkdir`, `filename_rmdir`, and the export boundary][ref-3]
- [Kprobe declarations][ref-4]

[ref-1]:
  https://docs.kernel.org/7.2/trace/kprobes.html

[ref-2]:
  https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c

[ref-3]:
  https://github.com/torvalds/linux/blob/v7.2/fs/namei.c

[ref-4]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h
