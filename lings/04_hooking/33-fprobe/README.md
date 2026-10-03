# 33 - Fprobe

## Why this step

Fprobe is the kernel's function entry/exit probe API built on ftrace. On modern
kernels it is the direct way to observe a function by name: no symbol lookup,
raw address filter, or hand-built ftrace registration sequence is needed.

Use kprobes when you need to probe an instruction. Use kretprobes when the
kprobe machinery must inspect a return value. Use fprobe when function-level
entry/exit tracing is the actual requirement.

```text
openat(2) ---> do_sys_openat2()
                   |
             ftrace trampoline
                   |
             fprobe entry handler ---> atomic counter
                   |
             do_sys_openat2() continues
```

## The API

An entry handler has this shape:

```c
static int on_entry(struct fprobe *fp, unsigned long entry_ip,
                    unsigned long ret_ip, struct ftrace_regs *regs,
                    void *entry_data)
{
    atomic_long_inc(&count);
    return 0;
}
```

Registration and cleanup are symmetric:

```c
static struct fprobe probe = {
    .entry_handler = on_entry,
};

ret = register_fprobe(&probe, "do_sys_openat2", NULL);
/* ... */
unregister_fprobe(&probe);
```

The filter is a function name or ftrace glob. `probe.nmissed` reports events
that could not be handled because the probe was already active on that task.

## Build it

Create a module that:

1. Registers one fprobe on `do_sys_openat2`.
2. Atomically counts entries in the callback.
3. Exposes the count as a plain number through `/proc/rk_fprobe`.
4. Removes the proc entry and unregisters the probe on exit.

Keep the callback tiny. It runs in the probed function's context and must not
sleep.

## Contract

The check loads the module, opens several files, verifies that the counter
increases, unloads the module, and verifies that the proc entry is gone.

## Verify

```bash
./rootkitlings check 33
```

> **What a defender sees**: fprobe is built on ftrace. Registered functions
> remain observable through the tracing subsystem, and the callback adds
> measurable work to the function path.

See also: [34 - rmdir trigger](../34-rmdir-trigger/README.md).
