# 09 - Publish a procfs entry

Procfs lets a module create a virtual path whose operations are implemented by kernel callbacks.

This module creates `/proc/rk_entry` during init and removes it before unload completes.

The operations table is intentionally empty so entry lifetime is isolated from reading and writing.

## Observe it

```sh
test -e /proc/rk_entry || echo absent
insmod hello.ko
ls -l /proc/rk_entry
cat /proc/rk_entry
rmmod hello
test -e /proc/rk_entry || echo absent
```

The path appears with mode `0444` while the module is loaded.

`cat` cannot return content because no read callback is registered.

## Entry lifetime

```text
module init                          module exit
    |                                    |
    v                                    v
proc_create() -> /proc/rk_entry -> proc_remove()
```

`proc_create("rk_entry", 0444, NULL, &entry_ops)` creates the entry at the root of procfs.

It returns a `struct proc_dir_entry *` on success and `NULL` on failure.

Procfs retains the address of `entry_ops`, so the table must remain valid for the complete entry lifetime.

`proc_remove()` prevents future operations from entering module callbacks during teardown.

## Implement it

In the workspace `hello.c`, the scaffold provides the module lifecycle, saved entry pointer, and empty operations table.

In `entry_init()`, create `rk_entry`, save the returned pointer, and return `-ENOMEM` when it is `NULL`.

In `entry_exit()`, pass that pointer to `proc_remove()`.

Remove the scaffold-only `(void)` references after using both objects.

## Verify

```sh
./rootkitlings check L09
```

The verification observes absence before load, presence after load, and absence after unload.

The path is visible in `/proc`, and its lack of useful operations does not make it less observable.

Next: [10 - Read from procfs](../10-proc-read/README.md).
