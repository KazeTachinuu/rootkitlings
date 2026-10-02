# 31 - Hook lifecycle

## Why this step

A real rootkit installs multiple hooks. If the third hook fails, the
first two must be rolled back. If exit forgets one hook, the kernel
crashes when the dangling callback fires.

## The concept

```
  Init: register hooks in order, rollback on failure

  register kp_read     --> ok
  register kp_write    --> FAIL
       |
       v
  unregister kp_read   (rollback: reverse order)
  return -EINVAL

  Exit: unregister in reverse order

  unregister kp_write  (last registered, first removed)
  unregister kp_read
```

This is the same goto-cleanup pattern from ling 06, applied to hooks.
A dangling hook (registered but never unregistered) crashes the kernel
when the callback fires after the module's code is freed.

## What you already know

From lings 27-30, you can register individual hooks. This ling
combines two kprobes and teaches proper rollback on partial failure.

## Build it

You need:

```c
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
```

Create a module that:

1. Installs two kprobes: one on `vfs_read`, one on `vfs_write`.
2. Each probe has its own counter.
3. `/proc/rk_lifecycle` returns both counters: `vfs_read=<n> vfs_write=<n>`.
4. Init rolls back on partial failure (second probe fails = unregister first).
5. Exit unregisters both in reverse order.

```c
static struct kprobe kp_read = {
    .symbol_name = "vfs_read",
    .pre_handler = read_handler,
};

static struct kprobe kp_write = {
    .symbol_name = "vfs_write",
    .pre_handler = write_handler,
};
```

The cleanup pattern: unregister in reverse order of registration.

## Predict

- What is the correct cleanup order?
- What happens if you unregister in the wrong order?
- How do you test the rollback path?


## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_lifecycle                 # hook state and counters
dmesg | tail
rmmod hello
dmesg | tail
```

## Contract

The check script verifies:

- `/proc/rk_lifecycle` contains `vfs_read=` and `vfs_write=` counters.
- Both counters are greater than 0 after triggering reads and writes.
- `/proc/rk_lifecycle` disappears after unload.

## Verify

```bash
./rootkitlings check 31
```

> **What a defender sees**: multiple hooks on different functions. Each
> one appears in debugfs or tracefs. A defender looking at `enabled_functions`
> sees the full hook surface.

See also: [Hiding](../../05_hiding/README.md).
