# Manage two named Kprobes

Observe two kernel functions without changing what they do:

- `vfs_mkdir` when a directory is created
- `filename_rmdir` when a directory is removed

The module keeps one flag for each function.
The flags start at zero on every load and change only while actions are enabled:

```text
active=1
actions=enabled
vfs_mkdir_seen=1
filename_rmdir_seen=0
```

Creating a directory sets only `vfs_mkdir_seen`.
Removing it then sets `filename_rmdir_seen`.
Both filesystem operations must still succeed normally.

## Names belong to a pinned kernel

This course runs Linux v7.2 on x86-64 with Kprobes, kallsyms, and debugfs enabled.
In that build:

- `vfs_mkdir` is present and exported to modules
- `filename_rmdir` is present but not exported to modules

An exported symbol is part of the interface that an external module may link against.
An internal symbol is not.
A named Kprobe is different: the Kprobe core resolves `.symbol_name` inside the running kernel and installs an observation point if that target is present and probeable.

Do not treat `filename_rmdir` as a stable module API.
Its name and behavior may change in another kernel build.
The checker verifies the release, architecture, configuration, symbol presence, and export surface used here.

Successful registration fills the `addr` field inside each managed `struct kprobe`.
Do not copy that address, call through it, log it, or publish it.
This exercise needs only the registered probe objects.

## Keep handlers bounded

A Kprobe pre-handler may run concurrently on several CPUs with preemption or interrupts disabled.
It must not sleep or perform work that may block.

Each handler in this exercise does only this:

1. read the action switch once
2. if enabled, set its own seen flag to `true`
3. return zero so the probed function continues normally

Do not allocate memory, access user memory, log, wait, take a sleeping lock, perform I/O, inspect registers, or change registers in these handlers.

Several CPUs may store `true` to the same flag.
The supplied state operations use `READ_ONCE()` and `WRITE_ONCE()`.
This is sufficient because every writer stores the same final value and no update depends on the previous value.
A counter would need an atomic read-modify-write operation or another design that protects the update.

The action switch is an admission rule, not probe removal.
Both probes remain registered while actions are disabled.
A handler that reads enabled may finish after a concurrent `disable` write.

## Acquire and release the complete set

`register_kprobes()` takes an array of probe pointers.
It registers them in order.
If one registration fails, Linux unregisters every earlier probe in the array before returning the failing error.
Do not unregister the first probe a second time on this error path.

Failure point 4 gives the second probe a name that does not exist.
Named lookup returns `-ENOENT`, and the batch operation removes the already registered first probe before returning.

The supplied module lifecycle is:

```text
allocate state
register both probes
publish procfs
```

If procfs publication fails, the module unregisters both probes before freeing state.
Unload removes procfs, unregisters both probes, then frees state.
`unregister_kprobes()` waits until the handlers can no longer use the borrowed state.

`/sys/kernel/debug/kprobes/list` independently shows registered probes.
Its lines include a kernel address and may include `[FTRACE]` or `[OPTIMIZED]`.
Those details are chosen by the kernel.
The checker uses only the probe type and exact symbol name, and verifies that this module adds one entry per target only while it is loaded.

## Your task

Edit one file:

- `instrumentation/probes.c`

Complete the two handlers, batch registration, and batch unregistration.

Run:

```bash
./rootkitlings check
```

The checker covers the pinned ABI contract, disabled and enabled observations, normal filesystem results, missing-symbol rollback, procfs rollback, unload, reload, two complete load cycles, and stale probe cleanup.

## Official Linux v7.2 references

- [Kprobes API, handler constraints, and debugfs registry](https://docs.kernel.org/7.2/trace/kprobes.html)
- [`register_kprobes()` rollback and named lookup](https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c)
- [`vfs_mkdir`, `filename_rmdir`, and the export boundary](https://github.com/torvalds/linux/blob/v7.2/fs/namei.c)
- [Kprobe declarations](https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h)
