# M01: Hide the module from lsmod

The first thing an investigator checks is `lsmod`. If your module
appears there, the game is over. This milestone teaches the idiomatic
technique for hiding a kernel module: removing its entry from the
kernel's internal module list.

## The technique

The kernel maintains a doubly-linked list of loaded modules. `lsmod`
reads `/proc/modules`, which iterates this list. The Diamorphine
rootkit popularized the simplest hiding technique:

```c
saved_prev = THIS_MODULE->list.prev;
list_del(&THIS_MODULE->list);
```

Two lines. The module disappears from `lsmod` and `/proc/modules`.

To restore visibility (for clean unload):

```c
list_add(&THIS_MODULE->list, saved_prev);
```

## What this does NOT hide

`list_del` only removes the module from the iteration list. The module
is still visible through:

- `/sys/module/rootkitlings/` (sysfs, created at module load)
- `/proc/kallsyms` (exported symbols)
- `/sys/kernel/debug/kprobes/list` (if you registered kprobes)
- Direct memory forensics

A thorough rootkit would need to clean all of these. For now, hiding
from `lsmod` is the minimum viable cloak that covers the most common
check.

## Why save prev

`list_del` poisons the prev and next pointers of the removed node
(sets them to `LIST_POISON1`/`LIST_POISON2`). If you need to re-insert,
you must save the predecessor before deletion. Without it, `list_add`
would crash the kernel.

## Control interface

The starter code creates `/proc/rootkitlings` (world-readable and
writable). Write commands to control cloaking:

| Command | Effect |
|---------|--------|
| `echo hide > /proc/rootkitlings` | Hide module from lsmod |
| `echo unhide > /proc/rootkitlings` | Restore module visibility |
| `cat /proc/rootkitlings` | Show current state (`hidden=true/false`) |

Invalid commands return an error without changing state.

## Your task

Implement `rk_module_cloak()` and `rk_module_uncloak()`. The proc
interface and command parsing are provided.
