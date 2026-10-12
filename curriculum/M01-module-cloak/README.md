# Hide the module from lsmod

The first thing an investigator checks is `lsmod`. If your module
appears there, the game is over. This milestone teaches the idiomatic
technique for hiding a kernel module: removing its entry from the
kernel's internal module list.

## The technique

The kernel maintains a doubly-linked list of loaded modules. `lsmod`
reads `/proc/modules`, which iterates this list.

```text
before:
  modules_list <-> [modA] <-> [rklings] <-> [modB] <-> ...
                                ^
                           saved_prev = list.prev

after list_del:
  modules_list <-> [modA] <-> [modB] <-> ...

  [rklings]  (unlinked, invisible to lsmod)

restore with list_add(list, saved_prev):
  modules_list <-> [modA] <-> [rklings] <-> [modB] <-> ...
```

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

- `/sys/module/rklings/` (sysfs, created at module load)
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

The starter code creates `/proc/rklings` (world-readable and
writable). Write commands to control cloaking:

| Command | Effect |
|---------|--------|
| `echo hide > /proc/rklings` | Hide module from lsmod |
| `echo unhide > /proc/rklings` | Restore module visibility |
| `cat /proc/rklings` | Show current state (`hidden=true/false`) |

Invalid commands return an error without changing state.

## Your task

Edit `main.c`.

Implement `rk_module_cloak()` and `rk_module_uncloak()`. The proc
interface and command parsing are provided.

Run:

```sh
./rklings check
```

## References

- [Linux v7.2 module list and `THIS_MODULE`][1]
- [Linux v7.2 linked-list API (`list_del`, `list_add`)][2]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/linux/module.h
[2]: https://github.com/torvalds/linux/blob/v7.2/include/linux/list.h
