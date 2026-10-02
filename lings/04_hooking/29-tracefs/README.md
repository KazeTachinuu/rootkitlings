# 29 - Debugfs interface

## Why this step

Kernel debug infrastructure at `/sys/kernel/debug/` is rarely inspected
by defenders. Rootkits can create entries there to stash state alongside
the kernel's own debug data, hiding in plain sight.

This ling teaches you to create debugfs directories and files, which
you will use later to expose rootkit state without `/proc` entries
that are easier to spot.

## The concept

```
  /sys/kernel/debug/
  +-- tracing/
  |   +-- available_filter_functions
  |   +-- enabled_functions
  |   +-- ...
  +-- kprobes/
  |   +-- list
  |   +-- enabled
  +-- rk_tracer/                 <-- your module's directory
      +-- state              <-- your custom file
```

Debugfs is mounted at `/sys/kernel/debug/`. Kernel subsystems put
diagnostic data here. By creating entries alongside them, a rootkit
hides its state in a place defenders rarely inspect.

## What you already know

From ling 27, you worked with kprobes. This ling uses debugfs, the
filesystem behind `/sys/kernel/debug/`, to create custom entries.

## Build it

You need:

```c
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
```

Create a directory and a file inside it:

```c
static struct dentry *dbg_dir;
static struct dentry *dbg_state;

dbg_dir = debugfs_create_dir("rk_tracer", NULL);
dbg_state = debugfs_create_file("state", 0444, dbg_dir,
                                NULL, &dbg_state_fops);
```

Debugfs files use `file_operations` (not `proc_ops`). The `state` file
should return an access counter.

Also create `/proc/rk_tracer` that reports the debugfs path.

Clean up in `tracer_exit` with `debugfs_remove_recursive()`.

## Hints

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_tracer
ls /sys/kernel/debug/rk_tracer/
cat /sys/kernel/tracing/available_filter_functions | head
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/sys/kernel/debug/rk_tracer/` directory and `state` file exist.
- The `state` file returns `accesses=N` where N increments on each read.
- `/proc/rk_tracer` contains `debugfs_dir=rk_tracer`.
- All debugfs entries are cleaned up after unload.

## Verify

```bash
./rootkitlings check 29
```

The check verifies the debugfs directory and state file exist,
the counter increments, and cleanup happens on unload.

> **What a defender sees**: `ls /sys/kernel/debug/` shows the extra
> directory. `mount -t debugfs` reveals it.

See also: [30 - Kprobe](../30-kprobe/README.md).
