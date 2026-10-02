# 32 - Module hiding

## Why this step

`lsmod` reads the kernel's module list. Remove your module from
that list and it disappears from `lsmod`. This is what Diamorphine,
Reptile, and every LKM rootkit does first.

## What you already know

From ling 06, you created `/proc/rk_cloak`. From the hooking lings,
you installed kernel hooks. This ling makes the module itself invisible.

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

## Predict

- Can you still `rmmod` a hidden module?
- Is the module still in `/sys/module/`?
- What about `/proc/modules`?


## Hints

<details>
<summary>Hint 1: what list_del_init actually does</summary>

`list_del_init` unlinks the node from its doubly-linked list and
re-initializes it to point to itself. After this call,
`list_for_each_entry` over the module list skips your module entirely.
`lsmod` and `/proc/modules` both walk this list.

</details>

<details>
<summary>Hint 2: why sysfs still shows the module</summary>

The module list (`THIS_MODULE->list`) and the sysfs kobject
(`THIS_MODULE->mkobj`) are separate data structures. Removing from
one does not affect the other. That is the weakness this ling teaches,
and ling 33 fixes it.

</details>

<details>
<summary>Hint 3: rmmod after hiding</summary>

In BusyBox, `rmmod` looks up the module by name in the kernel's module
list. If you removed yourself from that list, `rmmod hello` will fail
with "not found." You can still unload via `rmmod` if you re-insert
into the list first (toggle pattern).

</details>

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
./rootkitlings check 32
```

> **What a defender sees**: `lsmod` is clean, but `/sys/module/hello`
> still exists. Cross-view diffing catches this: compare `lsmod` output
> with `ls /sys/module/`. Any module in sysfs but not in lsmod is hidden.

See also: [33 - Sysfs cleanup](../33-sysfs-cleanup/README.md).
