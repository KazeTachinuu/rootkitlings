# 34 - rmdir trigger

Pumakit controls its rootkit with `rmdir`. The operator types
`rmdir /tmp/zarya.0` and the module intercepts it, parses the path
as a command, and acts. Defenders watch `kill` signals (Diamorphine
made that famous). Nobody watches `rmdir`.

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

The path is a userspace pointer. Use `strncpy_from_user` to copy it
into a kernel buffer before comparing:

```c
char kpath[128];
long len = strncpy_from_user(kpath, upath, sizeof(kpath));
if (len <= 0)
    return 0;
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
