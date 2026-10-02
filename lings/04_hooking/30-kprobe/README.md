# 30 - Kprobe

## Why this step

Kprobes insert a breakpoint at a function's entry point. When the
function is called, your handler runs first. This is the simplest
form of kernel hooking, used by Diamorphine and Reptile to intercept
syscall handlers.

## The concept

```
  userspace: open("/etc/passwd")
       |
       v
  do_sys_openat2()
       |
       +---> INT3 breakpoint (inserted by kprobe)
       |          |
       |          v
       |     pre_handler(regs)   <-- your callback
       |          |
       |          +---> counter++
       |
       v
  function runs normally
```

Unlike ling 27 (where the kprobe was immediately unregistered), here
the kprobe stays active and fires on every call to the target function.

## What you already know

From ling 27, you can resolve any symbol to an address. This ling
registers a persistent kprobe that stays active and counts calls.

## Build it

You need:

```c
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
```

Create a module that:

1. Registers a kprobe on `do_sys_openat2`.
2. The pre-handler increments a counter.
3. `/proc/rk_probe` returns the count of intercepted opens.
4. Exit unregisters the kprobe.

```c
static struct kprobe kp = {
    .symbol_name = "do_sys_openat2",
    .pre_handler = handler_pre,
};
```

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_probe                 # counter starts at 0
cat /dev/null                   # triggers do_sys_openat2
cat /dev/null
cat /proc/rk_probe                 # counter should have increased
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_probe` returns a counter as a plain number.
- The counter increments on file opens (`do_sys_openat2` calls).
- Counter keeps incrementing (not a one-shot).
- `/proc/rk_probe` disappears after unload.

## Verify

```bash
./rootkitlings check 30
```

> **What a defender sees**: `cat /sys/kernel/debug/kprobes/list`
> shows every registered kprobe. The function name and module are visible.

See also: [31 - Kretprobe](../31-kretprobe/README.md).
