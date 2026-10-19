# L10: Store Concealment Targets Safely

The commands in L09 perform an action immediately.
Concealment needs persistent runtime state: a set of names that later probes can
consult whenever a file, process, or port is listed.

This exercise builds one bounded, thread-safe target list and exposes it through
`/proc/rklings` so its behavior is easy to inspect.

## What you will observe

```console
root@victim:~# printf 'add secret.txt' > /proc/rklings
root@victim:~# printf 'add hidden_dir' > /proc/rklings
root@victim:~# cat /proc/rklings
secret.txt
hidden_dir
root@victim:~# printf 'remove secret.txt' > /proc/rklings
root@victim:~# cat /proc/rklings
hidden_dir
```

This lesson stores names only.
L11 connects the list to directory filtering.

## Workspace

The workspace contains one editable `main.c` with TODOs for:

- add, remove, exact match, and clear operations;
- a procfs read snapshot;
- command parsing and error propagation;
- spinlock initialization and procfs lifetime.

Read the data and locking model before implementing those operations.

## Represent the list

The list uses fixed storage:

```c
#define RK_TARGET_MAX 32
#define RK_NAME_MAX   64

struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};
```

Its live entries are always packed into indexes `0` through `count - 1`:

```text
count = 3

index      0            1            2            3 ... 31
       +----------+----------+----------+------------------+
       | alpha    | beta     | gamma    | unused           |
       +----------+----------+----------+------------------+
       <------------ live entries ----->
```

Fixed storage gives each operation a clear upper bound and avoids allocating
memory while holding the lock.

## Why every operation takes the lock

Kernel callbacks can run concurrently on different CPUs.
Without synchronization, an add and a remove can both read the same `count` and
then publish incompatible updates:

```text
CPU 0: add delta                    CPU 1: remove beta
+-----------------------+           +-----------------------+
| reads count = 3       |           | reads count = 3       |
| writes targets[3]     |           | shifts gamma to [1]   |
| writes count = 4      |           | writes count = 2      |
+-----------------------+           +-----------------------+

Final count depends on timing; the array and count may disagree.
```

Wrap every read or mutation of `targets` and `count` with:

```c
spin_lock_irqsave(&list->lock, flags);
/* bounded work that cannot sleep */
spin_unlock_irqrestore(&list->lock, flags);
```

A spinlock makes other CPUs wait instead of sleeping.
The `irqsave` form also prevents a local interrupt from re-entering code that
needs the same lock.

Code inside this critical section must be short and must not call an operation
that can sleep.

## Add one exact name

Reject empty input and a full list before changing state.
Use `strscpy` so an overlong name returns `-E2BIG` rather than creating a
silently truncated target:

```text
before add "delta"                 after success

count = 3                          count = 4
+-------+------+-------+------+   +-------+------+-------+-------+
| alpha | beta | gamma |      |   | alpha | beta | gamma | delta |
+-------+------+-------+------+   +-------+------+-------+-------+
```

Increment `count` only after the copy succeeds.
On `-ENOSPC` or `-E2BIG`, the original list must remain unchanged.

## Remove one exact name

Find the target with `strcmp`.
After a match, move all later slots one position to the left:

```text
remove "beta"

before                              after memmove
+-------+------+-------+           +-------+-------+--------+
| alpha | beta | gamma |           | alpha | gamma | unused |
+-------+------+-------+           +-------+-------+--------+
             +-------------->
count = 3                           count = 2
```

Use `memmove`, not `memcpy`, because the source and destination regions overlap.
Return `-ENOENT` without changing the list when no exact value matches.

`rk_target_match` performs the same exact comparison and returns a Boolean.
`rk_target_clear` needs only to set `count` to zero while holding the lock; old
bytes outside the live range are ignored.

## Print without sleeping under the spinlock

`seq_printf` may grow or otherwise manage the seq-file buffer, so do not call it
while holding a spinlock.
Take a bounded snapshot first:

```text
spin_lock
    |
    +-- copy count
    +-- copy live target bytes into local snapshot
    +-- spin_unlock
              |
              v
      seq_printf each snapshot entry
```

The snapshot is at most `32 × 64 = 2048` bytes.
After the lock is released, another CPU may update the live list, but the reader
still receives one internally consistent view.

## Parse the procfs commands

The write callback accepts exactly three forms:

| Input           | List operation             |
| --------------- | -------------------------- |
| `add <name>`    | Append one exact target    |
| `remove <name>` | Remove one exact target    |
| `clear`         | Set the live count to zero |

Copy the bounded userspace input as in L04 and remove one trailing newline.
Return `-EINVAL` for an unknown command.
Return the exact list error for a full list, missing target, empty name, or
overlong name.
Return the original write count only after success.

## Own the procfs lifetime

During module initialization:

1. Initialize the spinlock.
2. Set the count to zero.
3. Create `/proc/rklings` with mode `0666`.
4. Return `-ENOMEM` if publication fails.

During exit, remove the proc entry before the module code disappears.
Reloading the module creates a new empty list because the state is held only in
kernel memory.

## Implement it

Complete the TODOs in this order:

1. Implement bounded, atomic add.
2. Implement exact removal with `memmove`.
3. Implement exact match and clear.
4. Snapshot the live entries under lock in `rk_proc_show`.
5. Print the snapshot after releasing the lock.
6. Parse `add`, `remove`, and `clear` in `rk_proc_write`.
7. Propagate operation errors to the writer.
8. Initialize the list and own `/proc/rklings` across load and unload.

## Test it

Build and enter the VM:

```console
$ ./rklings run L10
```

Load the module and exercise ordering, removal, and clear:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# printf 'add alpha\n' > /proc/rklings
root@victim:~# printf 'add beta\n' > /proc/rklings
root@victim:~# printf 'add gamma\n' > /proc/rklings
root@victim:~# cat /proc/rklings
alpha
beta
gamma
root@victim:~# printf 'remove beta\n' > /proc/rklings
root@victim:~# cat /proc/rklings
alpha
gamma
root@victim:~# printf 'clear\n' > /proc/rklings
root@victim:~# test ! -s /proc/rklings && echo empty
empty
```

Removing an absent target must fail:

```console
root@victim:~# printf 'remove absent\n' > /proc/rklings
-bash: printf: write error: No such file or directory
```

## Validate

From the repository root:

```console
$ ./rklings validate L10
```

The checks cover mode `0666`, ordered additions, exact removal and compaction,
clear, special-character names, a fresh list after reload, the 32-entry limit,
and rejection of entry 33.

## Next

The module can now remember exact concealment targets, but ordinary listings
still show them.
L11 removes matching file names and process IDs from directory results while
leaving the underlying file or process intact.
