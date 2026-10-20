# L10: Remember Names to Hide

## Mission

L09 runs a command and immediately forgets it.
A hide command must remember its target so later directory listings can decide
whether to show that name.

Build the three operations behind that decision:

```text
add "secret.txt"       remember the name
match "secret.txt"     answer yes
remove "secret.txt"    forget the name
```

This exercise stores the targets.
L11 will use them to hide files and processes from listings.

## What you will observe

The supplied procfs adapter lets you exercise the list before it is connected to
a hook:

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

Nothing is hidden yet.
The output proves only that the module remembers the right names.

## Workspace

```text
exercises/L10-hide-targets
├── Kbuild
├── main.c
├── rklings_target.c
└── rklings_target.h
```

Only `rklings_target.c` is editable.
You implement adding, removing, and matching names.

The other files initialize the list and expose it through `/proc/rklings` for
testing.

## One list, three operations

The header gives the module a fixed list:

```c
struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};
```

`count` marks the stored part of the array:

```text
count = 2

targets    0: "secret.txt"    1: "hidden_dir"    2..31: unused
```

- Add writes the next free slot and advances `count`.
- Match compares a candidate with the stored names.
- Remove closes the gap and decreases `count`.

Names are exact targets: `secret` must not match `secret.txt`.
Adding the same name twice must not create a duplicate.

## Keep each decision consistent

A command may change the list while a hook is checking it on another CPU.
Each operation must therefore hold `list->lock` from its first access to its
final decision:

```c
unsigned long flags;

spin_lock_irqsave(&list->lock, flags);
/* inspect or change targets and count */
spin_unlock_irqrestore(&list->lock, flags);
```

Do only the short array operation while locked.
The supplied diagnostic reader takes its own locked copy before printing through
procfs.

## TODOs

```text
TODO 1  rklings_target.c:12:1  add one unique bounded name
TODO 2  rklings_target.c:18:1  remove one exact name
TODO 3  rklings_target.c:24:1  answer whether one exact name is stored
```

### TODO 1 — `rklings_target.c:12:1` — Add one target

Reject a missing or empty name with `-EINVAL`.

While holding the lock:

1. Return `0` if `strcmp` finds the exact name already stored.
2. Return `-ENOSPC` if all `RK_TARGET_MAX` slots are occupied.
3. Copy the name into the next slot with `strscpy`.
4. Advance `count` only if the copy succeeds.

Return the error from `strscpy` when the name does not fit.

### TODO 2 — `rklings_target.c:18:1` — Remove one target

```text
missing or empty  -> -EINVAL
exact match       -> remove it, return 0
no match          -> -ENOENT, change nothing
```

While holding the lock:

1. Find an exact `strcmp` match.
2. Move later slots left with `memmove`.
3. Decrease `count` and return `0`.

```text
before:  secret.txt  hidden_dir  notes
remove:              hidden_dir
after:   secret.txt  notes
```

### TODO 3 — `rklings_target.c:24:1` — Match one target

While holding the lock:

1. Compare the candidate with every stored name using `strcmp`.
2. Return `true` on an exact match.
3. Return `false` when no entry matches.

This is the operation L11 will call for every file name or PID returned by a
directory read.

## Test it

From the host:

```console
$ ./rklings run L10
```

Inside the guest, load the module and run the commands from **What you will
observe**.

The supplied `match` command exposes the boolean lookup as command success or
failure:

```console
root@victim:~# printf 'match hidden_dir' > /proc/rklings && echo match
match
root@victim:~# printf 'match hidden' > /proc/rklings 2>/dev/null || echo no-match
no-match
```

The first command succeeds.
The prefix fails because matching is exact.

## Validate

From the repository root:

```console
$ ./rklings validate L10
```

Validation checks ordered storage, duplicate suppression, exact matching,
removal, the 32-entry limit, reload, procfs permissions, and clean unload.

## Next

The module can now remember a name and answer whether it is targeted, but it
still changes no listing.
L11 removes matching file names and PIDs from directory results without deleting
the underlying objects.
