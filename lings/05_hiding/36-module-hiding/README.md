# 36 - Module hiding

## Why this step

`lsmod` reads the kernel's module list. Remove your module from
that list and it disappears from `lsmod`. Diamorphine is one public example of
this technique.

## What you already know

From ling 08, you created `/proc/rk_cloak`. From the hooking lings,
you installed kernel hooks. This ling removes one enumeration view.

## Build it

You need:

```c
#include <linux/list.h>
#include <linux/proc_fs.h>
```

Create a module that:

1. On load, removes itself from the module list.
2. After hiding, `lsmod` no longer shows the module.
3. `/proc/rk_cloak` returns `hidden` (procfs is a separate data structure).

```c
list_del_init(&THIS_MODULE->list);
```

Save `THIS_MODULE->list.prev` before deleting, in case you want to
re-insert later (toggle visibility).

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
lsmod                           # hello should NOT appear
cat /proc/rk_cloak                 # but this still works
ls /sys/module/hello            # sysfs still shows it (the weakness)
cat /proc/modules | grep hello  # also hidden here
```

## Contract

The check script verifies:

- `/proc/rk_cloak` returns `hidden`.
- Module does NOT appear in `lsmod` output.
- Module IS still visible in `/sys/module/hello` (that weakness is the lesson).

## Verify

```bash
./rootkitlings check 36
```

> **What a defender sees**: `lsmod` is clean, but `/sys/module/hello`
> still exists. Cross-view diffing catches this: compare `lsmod` output
> with `ls /sys/module/`. Any module in sysfs but not in lsmod is hidden.

See also: [37 - Sysfs hiding](../37-sysfs-hiding/README.md).
