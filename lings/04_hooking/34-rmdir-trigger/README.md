# 34 - rmdir trigger

Pumakit controls its rootkit with `rmdir`. The operator types
`rmdir /tmp/zarya.0` and the module intercepts it, parses the path
as a command, and acts. Defenders commonly watch known signal-based triggers
such as Diamorphine's. An unusual rmdir path is less conventional, not hidden.

## The pattern

Hook `__x64_sys_rmdir` with a kprobe (same mechanism as ling 30).
In the pre_handler, extract the userspace path from the register state.
If it starts with your prefix, parse the command and dispatch.

The syscall wrapper takes a single `struct pt_regs *` in rdi. That
inner pt_regs holds the actual syscall arguments:

```c
static int pre_rmdir(struct kprobe *p, struct pt_regs *regs)
{
    // regs->di is a pointer to the syscall's pt_regs
    struct pt_regs *uregs = (struct pt_regs *)regs->di;
    // uregs->di is the userspace pathname
    const char __user *upath = (const char __user *)uregs->di;
    ...
}
```

## Getting the path from userspace

The path is a userspace pointer. A kprobe handler cannot sleep, so use the
no-fault helper to copy it without taking a page fault:

```c
char kpath[128];
long len = copy_user_string_nofault(kpath, upath, sizeof(kpath));
if (len <= 0 || len >= (long)sizeof(kpath))
    return 0;
```

A return equal to the buffer size means the path was truncated and may not be
NUL-terminated. Reject it before calling `strcmp()`.

`copy_user_string_nofault()` is a small loop around the exported
`copy_from_user_nofault()` API: copy one byte, stop at NUL, and return an error
if any byte cannot be read. Linux 7.2 declares `strncpy_from_user_nofault()`
for built-in kernel code but does not export it to loadable modules.

```c
static long copy_user_string_nofault(char *dst, const char __user *src,
                                     size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        if (copy_from_user_nofault(&dst[i], src + i, 1))
            return -EFAULT;
        if (dst[i] == '\0')
            return i + 1;
    }
    return size;
}
```

## What to build

Hook `__x64_sys_rmdir`. Recognize three commands:

```
rmdir /tmp/.rk-ping     -> pr_info("pong\n")
rmdir /tmp/.rk-id       -> pr_info the uid of the calling process
rmdir /tmp/.rk-count    -> pr_info a counter that increments each call
```

Any other path passes through to the real rmdir.

## Contract

The check script verifies:

- `rmdir /tmp/.rk-ping` succeeds and "pong" appears in dmesg.
- `rmdir /tmp/.rk-id` succeeds and "uid=0" appears in dmesg.
- `rmdir /tmp/.rk-count` three times produces count 1, 2, 3 in dmesg.
- `rmdir /tmp/real-dir` still works normally (directory is removed).
- After unload, rmdir behaves normally.

## Verify

```bash
./rootkitlings check 34
```

> **What a defender sees**: the registered kprobe and command-shaped rmdir
> attempts. The trigger does not hide either the module or its log messages.

See also: [35 - Hook lifecycle](../35-hook-lifecycle/README.md).
