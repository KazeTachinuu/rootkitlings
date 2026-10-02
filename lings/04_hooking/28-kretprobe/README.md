# 28 - Kretprobe

## Why this step

A kprobe fires at function entry. A kretprobe fires at function return,
giving you the return value. You can observe (and later modify) what a
function returns. Rootkits use kretprobes to make `open()` on a hidden
file return `-ENOENT` instead of success.

## The concept

```
  kprobe (ling 27):              kretprobe (this ling):

  vfs_read() entry               vfs_read() entry
       |                              |
       +---> your handler             |  (no entry hook)
       |                              |
       v                              v
  function body                  function body
       |                              |
       v                              v
  return N                       return N
                                      |
                                      +---> ret_handler(regs)
                                      |       return value = N
                                      v
                                 caller gets N
```

A kretprobe replaces the return address on the stack with a trampoline.
When the function returns, your handler runs first and can inspect (or
later, modify) the return value.

## What you already know

From ling 27, you registered a kprobe on `do_sys_openat2`. This ling
hooks `vfs_read` at its return site instead, inspecting return values.

## Build it

You need:

```c
#include <linux/err.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
```

Create a module that:

1. Registers a kretprobe on `vfs_read`.
2. The return handler checks if the return value is negative (error).
3. Counts successful and failed reads separately.
4. `/proc/rk_retprobe` returns `success=<n> failed=<n>`.

```c
static struct kretprobe krp = {
    .handler     = ret_handler,
    .maxactive   = 20,
    .kp.symbol_name = "vfs_read",
};
```

In the handler, check if `regs_return_value(regs)` is negative.

## Predict

- Can you change the return value from the handler?
- How does kretprobe know when the function returns?
- What limits the number of simultaneous probed calls (`maxactive`)?


## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_retprobe                 # success=N failed=N
cat /dev/null                   # successful read
cat /nonexistent 2>/dev/null    # failed read
cat /proc/rk_retprobe                 # counters updated
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_retprobe` contains `success=` and `failed=` counters.
- After triggering file opens, `success` count is greater than 0.
- `/proc/rk_retprobe` disappears after unload.

## Verify

```bash
./rootkitlings check 28
```

> **What a defender sees**: same as kprobes. Visible in debugfs.
> But return probes are more powerful: a rootkit can make `read()`
> return zero bytes, silently suppressing file content.

See also: [29 - Write protection](../29-write-protection/README.md).
