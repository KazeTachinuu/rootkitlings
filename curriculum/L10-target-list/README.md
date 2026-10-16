# L10: Target List

A rootkit hides things: files, PIDs, ports, strings.
Before you can hide anything, you need a data structure to track what to hide.

## Why spinlocks

Kernel code runs concurrently.
A kretprobe handler can fire on any CPU at any time.
If one CPU adds a target while another CPU walks the list looking for a match,
you get torn reads, a corrupted count, or a panic.

A spinlock serializes access.
`spin_lock_irqsave` disables local interrupts and acquires the lock.
`spin_unlock_irqrestore` releases it and restores the interrupt state.
The `irqsave` variant is safe in both process context (your proc write handler)
and interrupt context (your kretprobe handlers).

```c
unsigned long flags;
spin_lock_irqsave(&list->lock, flags);
// critical section: only one CPU at a time
spin_unlock_irqrestore(&list->lock, flags);
```

## The data structure

```c
struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};
```

Fixed-size array, no heap allocation inside the lock.
Three operations:

- **add**: `strscpy` the name into `targets[count]`, increment count. Return `-ENOSPC` if full.
- **remove**: validate the index, `memmove` to shift entries down (no gaps), decrement count. `memmove` handles overlapping regions, `memcpy` does not.
- **match**: `strcmp` loop, return true on first hit.

All three acquire the lock, do the work, release the lock.

## Proc interface

`/proc/rklings` with permissions `0666` (everyone reads and writes).
Write handler parses three commands:

```text
  echo "add secret.txt" > /proc/rklings
       |
       v
  rk_proc_write --> parse "add " prefix --> rk_target_add("secret.txt")

  echo "remove 0" > /proc/rklings
       |
       v
  rk_proc_write --> parse "remove " prefix --> rk_target_remove(0)

  cat /proc/rklings --> rk_proc_show --> print each target, one per line
```

This is the same proc_ops pattern from L04, now with a richer command set.

## You write

`main.c`, ~160 lines:

- `struct rk_target_list` with spinlock, fixed-size array, count
- `rk_target_add()`, `rk_target_remove()`, `rk_target_match()`, `rk_target_clear()`
- `/proc/rklings` with proc_ops (read + write, `0666`)

## API

```c
#include <linux/spinlock.h>

#define RK_TARGET_MAX 32
#define RK_NAME_MAX   64

int  rk_target_add(struct rk_target_list *list, const char *name);
int  rk_target_remove(struct rk_target_list *list, int idx);
bool rk_target_match(struct rk_target_list *list, const char *name);
void rk_target_clear(struct rk_target_list *list);

spin_lock_init(&list->lock);
spin_lock_irqsave(&list->lock, flags);
spin_unlock_irqrestore(&list->lock, flags);
void strscpy(char *dest, const char *src, size_t count);
```

## Test it

The module is pre-loaded.
You are logged in as student.

```
$ echo "add secret.txt" > /proc/rklings
$ echo "add hidden_dir" > /proc/rklings
$ cat /proc/rklings
secret.txt
hidden_dir
$ echo "remove 0" > /proc/rklings
$ cat /proc/rklings
hidden_dir
$ echo "clear" > /proc/rklings
$ cat /proc/rklings
(empty)
```

## Validate

```
$ rklings validate l10
  [+] module loaded
  [+] /proc/rklings writable
  [+] add stores target
  [+] remove shifts entries (no gaps)
  [+] match finds stored target
  [+] clear empties list
  PASS
```

## Next

The target list stores names but nothing uses them yet.
L11 hooks getdents64 to hide files and PIDs from ls and ps.
