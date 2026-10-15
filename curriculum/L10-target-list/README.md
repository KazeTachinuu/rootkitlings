# Build a thread-safe target list

A rootkit hides things: files, PIDs, ports, strings.
Before you can hide anything, you need a data structure to track what to hide.
Build a fixed-size, spinlock-protected target list that every later concealment
module will reuse.

## The problem

Kernel code runs concurrently.
An interrupt can fire while you are modifying your target array.
A second CPU can call your match function while the first CPU is adding a name.
Without locking, you get torn reads, corrupted counts, or worse.

## spinlock_t and IRQ safety

A `spinlock_t` is the kernel's simplest mutual exclusion primitive.
It busy waits, so hold it briefly.

```
spinlock_t lock;
spin_lock_init(&lock);
```

Plain `spin_lock` / `spin_unlock` protect against concurrent threads, but not
against interrupts.
If an interrupt handler also touches your list, you deadlock.
Use the IRQ-safe variants:

```
unsigned long flags;
spin_lock_irqsave(&lock, flags);
// critical section
spin_unlock_irqrestore(&lock, flags);
```

These disable local interrupts while the lock is held, then restore the previous
interrupt state on unlock.

## The data structure

```c
#define RK_TARGET_MAX 32

struct rk_target_list {
    spinlock_t lock;
    char targets[RK_TARGET_MAX][64];
    int count;
};
```

Fixed-size array, no heap allocation inside the critical section, no complexity.
32 slots of 64 bytes each is plenty for a rootkit's needs.

## Operations

Four operations, all lock-protected:

- `rk_target_add(list, name)`: check count < max, `strscpy` the name in,
  increment count.
  Return 0 on success, -ENOSPC if full.
- `rk_target_remove(list, idx)`: validate index, `memmove` to close the gap,
  decrement count.
- `rk_target_match(list, name)`: iterate, `strcmp` each entry, return
  true/false.
- `rk_target_clear(list)`: set count to 0.

## Test interface

Expose the list through `/proc/rklings`:

- Write `"add <name>"` to add a target.
- Write `"remove <idx>"` to remove by index.
- Read to see all current targets, one per line.

Use `proc_create` with a `proc_ops` struct.
Remove it with `proc_remove` in your exit function.
Initialize the spinlock in `module_init`.

## Verify

```
./rklings check
```

```sh
$ echo "add secret.txt" > /proc/rklings
$ echo "add hidden_dir" > /proc/rklings
$ cat /proc/rklings
secret.txt
hidden_dir
$ echo "remove 0" > /proc/rklings
$ cat /proc/rklings
hidden_dir
```
