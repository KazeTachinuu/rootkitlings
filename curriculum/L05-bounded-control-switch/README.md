# Enable and disable module actions through procfs

The proc file can report state.
Now make it change one piece of state:

```text
active=1
actions=disabled
```

Writing `enable` changes the second line to `actions=enabled`.
Writing `disable` changes it back.
Both commands may have one trailing newline.

## Keep the state private

Add `bool actions_enabled` to the private `struct rk_state` and initialize it to `false` before the state is published.
Expose only `rk_state_actions_enabled()` and `rk_state_set_actions_enabled()` in `rootkit.h`; both use bool values.

The proc read and write callbacks may run at the same time.
Read the switch once before printing a record.
Parse a complete command before the single setter call; a rejected write must not change state.

Use `READ_ONCE()` for the getter and `WRITE_ONCE()` for the setter.
They mark the intentional concurrent access and prevent the compiler from combining, inventing, or splitting that source access.
They are not an atomic read-modify-write operation, a lock, a memory barrier, or a quiescence guarantee.

That limited contract fits this switch because it is one independent scalar and every write replaces the complete bool without depending on its previous value.
A rule involving several fields would need synchronization that protects the complete invariant.

## Define the write boundary

Replace `proc_create_single_data()` with `proc_create_data()` and an explicit `struct proc_ops`.
Keep `single_open()`, `seq_read()`, `seq_lseek()`, and `single_release()` for reads, then add the write callback.

The callback accepts exactly these byte sequences:

- `enable`
- `enable\n`
- `disable`
- `disable\n`

Apply checks in this order:

1. Reject a nonzero file offset with `-EINVAL`.
2. Reject a raw count above eight with `-E2BIG` before reading user memory.
3. Reject an empty write with `-EINVAL`.
4. Copy into an eight-byte local array.
   `copy_from_user()` returns the number of bytes it could not copy; any nonzero result means `-EFAULT` before commit.
5. Remove at most one final newline and compare the remaining bytes exactly.
6. Commit the bool once and return the original byte count.

Do not advance the file offset.
This keeps repeated commands on one open file descriptor valid.
Do not store the command string, add a mutex, or call `access_ok()` yourself; `copy_from_user()` performs the user-memory check.

Publish `/proc/rootkitlings` as mode `0644`.
It remains owned by root, so any user may inspect it while only root may control it.
Keep proc removal before state destruction.

## Your task

Edit these three files:

- `include/rootkit.h`
- `core/state.c`
- `interface/proc.c`

Run:

```bash
./rootkitlings check
```

The checker covers every accepted form, malformed input, exact error codes, partial user copies, offsets, state preservation, repeated writes, concurrent reads and writes, permissions, reload, and an open descriptor during unload.

## Official Linux v7.2 references

- [procfs declarations](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/linux/proc_fs.h?h=v7.2)
- [procfs callback wrappers](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/fs/proc/inode.c?h=v7.2)
- [user-memory access helpers](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/linux/uaccess.h?h=v7.2)
- [`READ_ONCE()` and `WRITE_ONCE()`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/memory-barriers.txt?h=v7.2)
- [Atomic types: when not to use them](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/atomic_t.txt?h=v7.2)
- [seq_file](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/filesystems/seq_file.rst?h=v7.2)
