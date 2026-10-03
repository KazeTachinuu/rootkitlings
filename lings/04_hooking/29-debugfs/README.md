# 29 - Debugfs

## Why this step

Debugfs lets kernel code expose diagnostic state through simple files. This
lesson creates one directory and one read-only file, then removes the whole
subtree during unload.

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

Debugfs is mounted at `/sys/kernel/debug/`. Its API is intentionally not a
stable userspace ABI; use it for diagnostics, not a production interface.

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

> **What a defender sees**: while mounted, `ls /sys/kernel/debug/` shows the
> extra directory directly. Debugfs is an observable interface, not hiding.

See also: [30 - Kprobe](../30-kprobe/README.md).
