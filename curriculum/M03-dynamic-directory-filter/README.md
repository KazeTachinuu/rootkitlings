# Hide files and PIDs with dynamic target lists

The L14 directory filter hides a single hardcoded filename. A real
rootkit needs to hide arbitrary files and PIDs at runtime, decided by
the operator. This milestone introduces dynamic target lists: a bounded
array protected by a spinlock that every hide command reuses.

## Dynamic target lists

The `struct rk_target_list` holds up to 16 entries of up to 64 bytes
each. The operator adds entries via proc write (`hide file <prefix>`,
`hide pid <N>`) and removes them by index (`unhide file <index>`).

The list is protected by a spinlock, not a mutex, because the match
functions are called from kretprobe return handlers. Kretprobe handlers
can run in interrupt context (the kretprobe trampoline fires on
function return, which may happen during interrupt processing). Using
`spin_lock_irqsave` / `spin_unlock_irqrestore` is mandatory.

```
spin_lock_irqsave(&list->lock, flags);
// iterate, match, or modify
spin_unlock_irqrestore(&list->lock, flags);
```

Using plain `spin_lock` would deadlock if the kretprobe fires while
the same CPU already holds the lock from a proc write.

## Substring matching vs exact matching

File hiding uses substring matching: `hide file rkling` hides every
directory entry whose name contains "rkling". This lets one rule
cover `rkling_persist`, `rkling_shell`, `rkling_agent`, etc.

PID hiding uses exact numeric matching: `hide pid 1234` hides the
directory entry named "1234" from /proc listings. The entry's name is
parsed as an unsigned integer and compared against the stored value.

## The getdents64 kretprobe

```text
userspace buffer (from getdents64 return):

+----------+----------+----------+----------+----------+
| "."      | "secret" | "normal" | "rkling" | "data"   |
| reclen=32| reclen=40| reclen=32| reclen=40| reclen=32|
+----------+----------+----------+----------+----------+
                 ^                      ^
                 match                  match

after filtering (compacted, return value adjusted):

+----------+----------+----------+
| "."      | "normal" | "data"   |
| reclen=32| reclen=32| reclen=32|
+----------+----------+----------+
```

The kretprobe hooks `__x64_sys_getdents64`. The return handler:
1. Copies the dirent buffer from userspace
2. Iterates the packed `linux_dirent64` records
3. Removes matching entries by compacting the buffer
4. Copies back and adjusts the return value

The dirent filter logic is inlined from the L14 pattern: each record
has a `d_reclen` field giving its size, and the `d_name` field starts
at a fixed offset. When removing an entry, the `d_off` of the previous
retained entry is updated to maintain the linked-list invariant.

## Control interface

The starter code creates `/proc/rklings` (world-readable and
writable). Write commands to manage the target lists:

| Command | Effect |
|---------|--------|
| `echo "hide file rkling" > /proc/rklings` | Hide entries starting with "rkling" |
| `echo "hide pid 1234" > /proc/rklings` | Hide PID 1234 from /proc |
| `echo "unhide file 0" > /proc/rklings` | Remove file target at index 0 |
| `echo "unhide pid 0" > /proc/rklings` | Remove PID target at index 0 |
| `cat /proc/rklings` | Show current target lists with indices |

Invalid commands return an error without changing state.

## Your task

Implement the four functions in `target_list.c` and the
`rk_dirent_should_hide` matcher in `main.c`. The kretprobe plumbing,
proc interface, and dirent filter loop are provided.

Run:

```sh
./rklings check
```

## References

- [Linux v7.2 spinlock API][1]
- [Linux v7.2 `getdents64` and directory record layout][2]

[1]: https://docs.kernel.org/7.2/locking/spinlocks.html
[2]: https://github.com/torvalds/linux/blob/v7.2/fs/readdir.c
