# 32 - Ftrace

## Why this step

Ftrace is the kernel's official tracing framework. By registering a
callback with `FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_IPMODIFY | FTRACE_OPS_FL_RECURSION`,
you can intercept any function and redirect execution to your own
implementation. This is what PUMAKIT, KoviD, and Syslogk use.

## What you already know

From ling 28, you can resolve symbol addresses. From ling 29, you
hooked functions with kprobes. Ftrace is more powerful: it can redirect
`regs->ip` to a replacement function, not just observe.

## The concept

```
  userspace: open("/etc/passwd")
       |
       v
  do_sys_openat2()
       |
       +---> ftrace trampoline fires
       |          |
       |          v
       |     your_callback(ip, parent_ip, ops, fregs)
       |          |
       |          +---> increment counter
       |          +---> (later lings: redirect regs->ip)
       |
       v
  function runs normally
```

The setup sequence:

```
  1. ftrace_set_filter_ip(&ops, addr, 0, 0)   // arm on this address
  2. register_ftrace_function(&ops)            // start intercepting
     ...
  3. unregister_ftrace_function(&ops)          // stop intercepting
  4. ftrace_set_filter_ip(&ops, addr, 1, 0)   // clear filter
```

## Build it

You need:

```c
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
```

Create a module that:

1. Resolves `do_sys_openat2` via the kprobe lookup trick (ling 28).
2. Hooks it via ftrace with `ftrace_set_filter_ip` + `register_ftrace_function`.
3. The callback increments a counter.
4. `/proc/rk_ftrace` returns the count of intercepted opens.
5. Exit unregisters the ftrace callback and clears the filter.

The ftrace callback signature:

```c
static void notrace hook_callback(unsigned long ip, unsigned long parent_ip,
                                  struct ftrace_ops *ops,
                                  struct ftrace_regs *fregs)
```

Required flags:

```c
static struct ftrace_ops ops = {
    .func  = hook_callback,
    .flags = FTRACE_OPS_FL_SAVE_REGS
           | FTRACE_OPS_FL_IPMODIFY
           | FTRACE_OPS_FL_RECURSION,
};
```

## Hints

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_ftrace                           # counter value
cat /dev/null; cat /dev/null; cat /dev/null
cat /proc/rk_ftrace                           # counter should be higher
cat /sys/kernel/tracing/enabled_functions  # your hook is visible here
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_ftrace` returns a counter as a plain number.
- The counter increments on file opens (`do_sys_openat2` calls).
- `/proc/rk_ftrace` disappears after unload.

## Verify

```bash
./rootkitlings check 30
```

> **What a defender sees**: `cat /sys/kernel/tracing/enabled_functions`
> shows `do_sys_openat2` with your module listed. This is the most direct
> detection method for ftrace hooks.

See also: [31 - Hook lifecycle](../31-hook-lifecycle/README.md).
