# Set a tripwire on one directory removal

A watched directory may be removed, or a failed call may leave it untouched.
Set a tripwire that tells those outcomes apart without changing `rmdir()`.

Watch one exact path, carry the match from entry to return, and record the
result.
Success proves that concurrent calls keep their observations separate.

The selected path is:

```text
/tmp/rklings-rmdir-watch
```

```text
active=1
actions=enabled
selected_success_seen=1
selected_failure_seen=0
return_capacity=16
returns_missed=0
```

The `seen` fields reset on load and are flags, not counters.

## One invocation, two handlers

A **kretprobe** fires twice: once when the function is entered, once when it
returns.
This lets you carry data from entry to return, so you can correlate arguments
with results.

This lesson probes the native x86-64 wrapper `__x64_sys_rmdir`.
Concurrent calls forbid a global scratch variable.

Each active invocation instead owns this private record:

```c
struct rk_rmdir_instance {
	bool selected;
};
```

Set `data_size` so each `kretprobe_instance` owns this record.
Entry writes `instance->data`; the matching return reads it.

## Decode the pinned syscall wrapper

On the pinned x86-64 kernel, the wrapper's sole argument points to saved
userspace syscall registers.

```text
entry registers
    |
    `-- kernel argument 0
            |
            `-- saved syscall registers
                    |
                    `-- di: userspace path pointer
```

Read the wrapper argument with `regs_get_kernel_argument(registers, 0)`.
Then read `di` from that saved frame for syscall argument zero.
This pinned ABI does not describe the 32-bit compatibility wrapper.

The symbol is not module-linkable.
Named registration resolves it internally; never copy, publish, or call its
address.

## Copy without taking a userspace fault

Probe callbacks cannot use sleeping `copy_from_user()`.
Keep the supplied 64-byte loop over exported `copy_from_user_nofault()`.
It requires a NUL within the bound and an exact byte match.
This is an entry-time snapshot, so another thread may change the buffer before
`rmdir()` copies it.
The pinned build does not export `strncpy_from_user_nofault()`.

## Admit work once

Read the action switch once.
When disabled, return nonzero to skip this call's return handler (the entry
handler's return value controls whether the return handler fires).
That value is not an errno and does not change `rmdir()`.
When enabled, initialize the record, classify the path, and return zero.
A call admitted before `disable` may record its result afterward.

Use `regs_return_value()` at return: zero succeeds and negative is an errno.
Unselected paths and bad userspace pointers change neither flag.

Do not change registers, skip the system call, log, allocate memory, take a
sleeping lock, perform I/O, or dispatch a command from either handler.

## Capacity and missed returns

If all instances are busy, the kernel increments `nmissed` and skips both
learner handlers for that call.
`returns_missed` reports this condition, not failed syscalls.
Allocation precedes the action check, so a miss need not be a selected call.

## Lifetime and rollback

The module acquires resources in this order:

```text
allocate state
register Kretprobe
publish procfs
```

Release in reverse order.
If proc publication fails, unregister the probe before freeing state.

The debugfs Kprobe registry provides independent lifecycle evidence.

## Your task

Edit one file:

- `instrumentation/return_probe.c`

Complete the entry handler, return handler, registration, and unregistration.
Keep the supplied bounded copy helper, exact path, capacity, and descriptor
ownership unchanged.

Run:

```sh
./rklings check
```

The checker stresses ABI decoding, hostile pointers, concurrent correlation,
rollback, unload during calls, reload, and registry cleanup.

## References

- [Kretprobe entry, return, private-data, capacity, and registry contracts][1]
- [`register_kretprobe()`, `nmissed`, and unregistration][2]
- [Kretprobe data structures][3]
- [Native x86-64 syscall-wrapper register mapping][4]
- [`regs_return_value()` on x86-64][5]
- [`rmdir()` implementation][6]
- [`copy_from_user_nofault()` and the unexported string helper][7]
- [Return-hook shutdown and deferred reclamation][8]

[1]: https://docs.kernel.org/7.2/trace/kprobes.html
[2]: https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c
[3]: https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h
[4]:
  https://github.com/torvalds/linux/blob/v7.2/arch/x86/include/asm/syscall_wrapper.h
[5]: https://github.com/torvalds/linux/blob/v7.2/arch/x86/include/asm/ptrace.h
[6]: https://github.com/torvalds/linux/blob/v7.2/fs/namei.c#L5138-L5142
[7]: https://github.com/torvalds/linux/blob/v7.2/mm/maccess.c
[8]: https://github.com/torvalds/linux/blob/v7.2/kernel/trace/rethook.c
