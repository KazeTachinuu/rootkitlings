# 32 - Decode a syscall argument in a probe

A syscall probe can inspect the userspace arguments of the task crossing that syscall.
This module treats selected `rmdir()` paths as commands while allowing the real system call to continue normally.

## Observe it

```bash
rmdir /tmp/.rk-ping
# dmesg: pong

rmdir /tmp/.rk-id
# dmesg: uid=0

rmdir /tmp/.rk-count
# dmesg: count=1
```

The paths need not exist because the pre-handler runs before `rmdir()` validates them.
A normal existing directory is still removed because the handler does not alter registers or skip the syscall.

## Follow the argument

On x86-64, the kprobe sees the kernel wrapper's registers.
Its first argument points to the saved userspace syscall registers, whose first argument contains the path pointer.

```text
probe regs->di ---> userspace pt_regs->di ---> userspace path
```

The path still belongs to userspace.
A probe cannot take a page fault, so ordinary `copy_from_user()` is unsafe in this context.
The provided bounded helper copies with `copy_from_user_nofault()` and accepts only a terminated string that fits the buffer.

The `/tmp/.rk-` prefix is removed before `dispatch()` compares `ping`, `id`, or `count`.
This register layout is architecture-specific and is intentionally limited to the supported x86-64 kernel.

## Implement it

Decode the nested registers in `pre_rmdir()` and assign the userspace path to `upath`.
Leave the bounded no-fault copy and dispatch logic unchanged.

## Verify

```bash
./rootkitlings start L32
$EDITOR .rootkitlings/work/syscall-argument-decoding/hello.c
./rootkitlings check L32
```

The check invokes every command, verifies the counter sequence, proves ordinary `rmdir()` still works, and tests behavior after unload.
