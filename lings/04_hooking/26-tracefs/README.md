# 26 - Debugfs interface

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

From ling 24, you worked with kprobes. This ling uses debugfs, the
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

## Predict

- Where does the `rk_tracer/` directory appear in the filesystem?
- Why use `file_operations` instead of `proc_ops`?
- Would a defender see these entries in normal debug output?


## Hints

<details>
<summary>Hint 1: debugfs_create_file uses file_operations, not proc_ops</summary>

Debugfs files use `struct file_operations` (the same type as char
devices in ling 12), not `struct proc_ops`. The read callback has the
standard VFS signature. You can still use `simple_read_from_buffer` to
serve the formatted string.

</details>

<details>
<summary>Hint 2: cleanup with one call</summary>

`debugfs_remove_recursive(dbg_dir)` removes the directory and all files
inside it. Call it in `tracer_exit` before `proc_remove`. You do not need
to remove individual files.

</details>

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
./rootkitlings check 26
```

The check verifies the debugfs directory and state file exist,
the counter increments, and cleanup happens on unload.

> **What a defender sees**: `ls /sys/kernel/debug/` shows the extra
> directory. `mount -t debugfs` reveals it.

See also: [27 - Kprobe](../27-kprobe/README.md).
