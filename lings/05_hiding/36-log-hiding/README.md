# 36 - Log hiding

## Why this step

Rootkits log messages with `pr_info`. Defenders read `dmesg`. A real
rootkit hooks the syslog syscall to filter its own messages from
`dmesg` output, so defenders never see them.

Reptile hooks `devkmsg_read` to filter lines. This ling hooks
`__x64_sys_syslog` (what busybox `dmesg` calls) and removes any line
containing `rk:` from the output buffer.

## What you already know

From ling 35, you filtered file content. This ling applies the same
concept to the kernel log itself.

## Build it

You need:

```c
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
```

Use the kprobe trick to resolve `__x64_sys_syslog`, then hook it
with ftrace (the same pattern from ling 30).

In the hook, call the original syscall. For `SYSLOG_ACTION_READ_ALL`
(type 3) and `SYSLOG_ACTION_READ` (type 2), scan the returned buffer
line by line. Remove any line containing `HIDE_PREFIX` by shifting
subsequent lines forward.

```c
// After original returns, copy buffer from user, remove matching lines,
// copy back, adjust return value
```

Also create `/proc/rk_logfilter` that reports how many lines were hidden.

## Hints

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
./rootkitlings check 36
```

The check loads the module and verifies that `dmesg` does NOT show
any `rk:` lines, even though the module logs them.

> **What a defender sees**: gaps in the kernel log timeline. Monotonic
> timestamps jump. A persistent syslog forwarding to a remote host
> catches messages before they can be filtered.

See also: [37 - Process hiding](../37-process-hiding/README.md).
