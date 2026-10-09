# Correlate an rmdir path with its result

Observe one exact `rmdir()` path without changing the system call.

The selected path is:

```text
/tmp/rootkitlings-rmdir-watch
```

The module records whether that path has succeeded and whether it has failed:

```text
active=1
actions=enabled
selected_success_seen=1
selected_failure_seen=0
return_capacity=16
returns_missed=0
```

The two `seen` fields start at zero on each load and only change to one.
They are observations, not event counters.

## One invocation, two handlers

A Kretprobe places an entry probe on a function and arranges a matching callback when that invocation returns.
This lesson probes the native x86-64 wrapper `__x64_sys_rmdir`.

The entry handler learns which userspace path belongs to the invocation.
The return handler learns its result.
Those callbacks may overlap with callbacks for other tasks, so a global scratch variable cannot join them correctly.

Each active invocation instead owns this private record:

```c
struct rk_rmdir_instance {
	bool selected;
};
```

`data_size` asks the Kretprobe core to reserve that record in every `kretprobe_instance`.
The entry handler writes `instance->data`, and the matching return handler reads the same instance.

## Decode the pinned syscall wrapper

On this course's x86-64 Linux v7.2 kernel, `__x64_sys_rmdir` receives one kernel function argument: a pointer to the saved userspace syscall registers.

```text
entry registers
    |
    `-- kernel argument 0
            |
            `-- saved syscall registers
                    |
                    `-- di: userspace path pointer
```

Use `regs_get_kernel_argument(registers, 0)` for the wrapper's kernel argument.
Then read `di` from the saved register frame for the first native x86-64 syscall argument.
This layout is specific to the pinned architecture and wrapper ABI.
It does not describe the 32-bit compatibility wrapper.

`__x64_sys_rmdir` exists in the pinned kernel but is not exported as a module-linkable API.
Named Kretprobe registration resolves it inside the Kprobe core.
Do not copy, publish, or call its address.

## Copy without taking a userspace fault

The path pointer still belongs to userspace.
Probe callbacks cannot use ordinary `copy_from_user()` because it may resolve a page fault by sleeping.

The supplied helper copies at most 64 bytes, one byte at a time, through the GPL-exported `copy_from_user_nofault()`.
It stops at the first NUL byte and rejects a fault or a path that does not terminate inside the bound.
Only a complete byte-for-byte match selects the watched path.
This is an entry-time snapshot of the userspace bytes, not a resolved pathname or an immutable identity.
If another thread changes that buffer before `rmdir()` performs its own pathname copy, the observation can differ from the pathname the system call eventually uses.

Linux v7.2 also contains `strncpy_from_user_nofault()`, but the pinned build does not export it to external modules.
The exercise therefore does not pretend that helper is available.

## Admit work once

The entry handler reads the action switch once.
When actions are disabled, it returns nonzero.
For a Kretprobe entry handler, nonzero means that this invocation gets no return callback.
It is not an errno and does not change `rmdir()`.

When actions are enabled, the handler initializes the private record, classifies the path, and returns zero.
The return handler then runs for that same invocation.
A call admitted just before `disable` may finish and record its result afterward.

The return handler obtains the architecture return value with `regs_return_value()`.
For `rmdir()`, zero is success and a negative value is an errno.
Unselected paths and malformed userspace pointers never change either observation flag.

Do not change registers, skip the system call, log, allocate memory, take a sleeping lock, perform I/O, or dispatch a command from either handler.

## Capacity and missed returns

This probe preallocates 16 return instances.
That exceeds the checker's bounded workload of eight concurrent callers.

If all instances are occupied, the kernel increments `nmissed` and lets the function continue without calling either learner handler for that invocation.
The status field `returns_missed` publishes that diagnostic.
It does not count failed system calls.
The allocation attempt happens before the entry handler reads the action switch, so a missed invocation can be one that the switch would have rejected.
Zero means the observed workload fit the configured capacity; a nonzero value means some wrapper invocations could not reach the admission check, not necessarily that a selected result was lost.

## Lifetime and rollback

The module acquires resources in this order:

```text
allocate state
register Kretprobe
publish procfs
```

It releases them in reverse order.
The borrowed state pointer is installed before registration and remains valid until after `unregister_kretprobe()`.

Failure point 4 replaces the target with a name that does not exist.
Registration returns `-ENOENT`, and no proc entry or probe remains.
If proc publication fails, the module unregisters the Kretprobe before freeing state.

`/sys/kernel/debug/kprobes/list` independently shows one return probe on `__x64_sys_rmdir` only while the module is loaded.

## Your task

Edit one file:

- `instrumentation/return_probe.c`

Complete the entry handler, return handler, registration, and unregistration.
Keep the supplied bounded copy helper, exact path, capacity, and descriptor ownership unchanged.

Run:

```bash
./rootkitlings check
```

The checker covers the pinned ABI and export boundary, disabled and enabled behavior, exact and near-match paths, hostile userspace pointers, concurrent entry/return correlation, registration and procfs rollback, unload during calls, reload, and registry cleanup.

## Official Linux v7.2 references

- [Kretprobe entry, return, private-data, capacity, and registry contracts](https://docs.kernel.org/7.2/trace/kprobes.html)
- [`register_kretprobe()`, `nmissed`, and unregistration](https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c)
- [Kretprobe data structures](https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h)
- [Native x86-64 syscall-wrapper register mapping](https://github.com/torvalds/linux/blob/v7.2/arch/x86/include/asm/syscall_wrapper.h)
- [`regs_return_value()` on x86-64](https://github.com/torvalds/linux/blob/v7.2/arch/x86/include/asm/ptrace.h)
- [`rmdir()` implementation](https://github.com/torvalds/linux/blob/v7.2/fs/namei.c#L5138-L5142)
- [`copy_from_user_nofault()` and the unexported string helper](https://github.com/torvalds/linux/blob/v7.2/mm/maccess.c)
- [Return-hook shutdown and deferred reclamation](https://github.com/torvalds/linux/blob/v7.2/kernel/trace/rethook.c)
