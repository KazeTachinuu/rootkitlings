# 40 - Log hiding

## Why this step

Rootkits log messages with `pr_info`. Defenders read `dmesg`. A real
rootkit can hook the syslog syscall to filter selected messages from one
`dmesg` path. Other readers and external collectors remain independent views.

## What you already know

From ling 39, you filtered file content via a kretprobe on sys_read.
This ling applies the same pattern to the kernel log: hook the syslog
syscall and strip lines containing your prefix from the output buffer.

## The concept

BusyBox `dmesg` calls the syslog syscall with type 3
(`SYSLOG_ACTION_READ_ALL`) or type 2 (`SYSLOG_ACTION_READ`). The
kernel fills a userspace buffer with log lines. Your kretprobe lets
the original syscall fill the buffer, then removes matching lines:

```
  dmesg
    |
    v
  __x64_sys_syslog(type=3, buf, len)
    |
    entry_handler:
    +---> save type (uregs->di) and buf (uregs->si)
    |
    original syslog runs, fills buf with log lines
    |
    ret_handler:
    +---> only act on type 2 or 3
    +---> copy_from_user_nofault
    +---> walk line by line (split on '\n')
    +---> memmove to remove lines containing "rk:"
    +---> copy_to_user_nofault, update return value
    |
    v
  dmesg output: no "rk:" lines visible
```

## Build it

You need:

```c
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
```

Register a kretprobe on `__x64_sys_syslog`. The context struct stores
the syslog `type` and the userspace buffer pointer, captured via
double indirection in the entry handler (same pattern as ling 38).

Allocate callback scratch space with `GFP_ATOMIC` and use no-fault user-copy
helpers. On failure, preserve the original syslog result.

The return handler:

1. Checks type is 2 or 3, and return value is positive.
2. Copies the log buffer from userspace, null-terminates it.
3. Walks the buffer line by line (look for `'\n'` with `memchr`).
4. For each line containing `HIDE_PREFIX` (use `strnstr`), skips it
   by not copying it forward. Otherwise, `memmove` to close gaps.
5. Updates the return value to the new (shorter) length.

Also create `/proc/rk_logfilter` that reports `hidden=N`.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
dmesg | grep "rk:"              # no lines with "rk:" visible
cat /proc/rk_logfilter                 # shows hidden line count
echo "test line" > /dev/kmsg
dmesg | tail                    # test line visible, rk: lines hidden
rmmod hello
dmesg | grep "rk:"              # now rk: lines reappear
```

## Contract

The check script verifies:

- `/proc/rk_logfilter` must output lines in this format:
  ```
  hidden=N
  ```
- The module hooks syslog so that `dmesg` never shows lines containing `rk:`.
- `/proc/rk_logfilter` must contain `hidden=N` where N is the count of filtered lines.
- Non-module log entries (like `This marker should survive`) remain visible.

## Verify

```bash
./rootkitlings check 40
```

The check loads the module and verifies that `dmesg` does NOT show
any `rk:` lines, even though the module logs them.

> **What a defender sees**: gaps in the kernel log timeline. Monotonic
> timestamps jump. A persistent syslog forwarding to a remote host
> catches messages before they can be filtered.

See also: [41 - Process hiding](../41-process-hiding/README.md).
