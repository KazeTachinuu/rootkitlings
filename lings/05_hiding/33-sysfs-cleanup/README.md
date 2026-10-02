# 33 - Sysfs cleanup

## Why this step

Ling 32 hid the module from `lsmod`, but `/sys/module/hello` still
exists. This ling removes that entry too, closing the cross-view gap.
Diamorphine and Reptile both perform this step.

## The concept

```
  Two separate data structures expose your module:

  1. Module list (THIS_MODULE->list)
     lsmod, /proc/modules
     +-- hello  <-- list_del_init removes this (ling 32)

  2. Sysfs kobject (THIS_MODULE->mkobj.kobj)
     /sys/module/hello/
     +-- parameters/
     +-- sections/
     +-- ...    <-- kobject_del removes this (this ling)

  Both must be cleaned for full hiding.
```

## What you already know

From ling 32, you removed your module from the module list. This ling
adds kobject removal so both `lsmod` and `ls /sys/module/` are clean.

## Build it

You need:

```c
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
```

Create a module that:

1. Hides from the module list (ling 32).
2. Removes its sysfs kobject.
3. After both, `lsmod` and `ls /sys/module/` are clean.

```c
list_del_init(&THIS_MODULE->list);
kobject_del(&THIS_MODULE->mkobj.kobj);
```

## Predict

- Can you still `rmmod hello` after removing the kobject?
- Is `/proc/modules` consistent with `/sys/module/`?
- What about `cat /proc/kallsyms | grep hello`?


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
./rootkitlings check 33
```

> **What a defender sees**: both views are clean. But the module's
> symbols may still appear in `/proc/kallsyms`, and the module's
> memory is still mapped. `crash` or Volatility finds it.

See also: [34 - Getdents64](../34-getdents64/README.md).
