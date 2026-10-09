# Omit one live probe from a proc view

The module still registers the two Kprobes from the previous exercise:

- `vfs_mkdir`
- `filename_rmdir`

This exercise changes only the list rendered by `/proc/rootkitlings`.
The `filename_rmdir` probe must remain registered and active, but its `listed_probe` row must not appear.

A fresh load must return exactly:

```text
active=1
actions=disabled
vfs_mkdir_seen=0
filename_rmdir_seen=0
listed_probe=vfs_mkdir
```

After enabling actions and creating then removing the controlled directory, both observation flags must be one:

```text
active=1
actions=enabled
vfs_mkdir_seen=1
filename_rmdir_seen=1
listed_probe=vfs_mkdir
```

The missing row does not mean that the probe is absent.
It means only that this procfs renderer omitted one item.

## One object, two views

The supplied probe catalog is the source of identity for registration and display.
Its IDs select the two actual `struct kprobe` descriptors, and `rk_probe_name()` reads each descriptor's registered name.
Do not add another symbol-name table or compare rendered strings.

The two views have different owners:

- `/proc/rootkitlings` is this module's selected view of its catalog.
- `/sys/kernel/debug/kprobes/list` is the kernel Kprobe registry.

The checker uses the second view as independent evidence.
It also executes both target operations and checks both observation flags.
The omitted `filename_rmdir` row therefore coexists with a registered probe and a live handler.

The deliberately nonexistent failure symbol is a negative control.
It must appear in neither view.

There is no supported userspace command here for directly looking up one registered Kprobe.
The evidence comes from comparing inventories and behavior, not from claiming a direct object lookup.

## How `SEQ_SKIP` changes one item

The supplied `seq_file` iterator visits exactly two catalog IDs.
Its show function receives one ID at a time.

On Linux v7.2, returning `SEQ_SKIP` tells the seq core to discard anything emitted for that item and continue with the next item.
Make the decision before writing anything so the suppression is local and clear.
Every non-omitted ID reaches the single `listed_probe` formatter.

This is a fixed view rule.
Do not add a hide command, a second state switch, or a new failure point.
Do not remove a probe, change the catalog, mutate module or kernel lists, or filter directories.

The action switch remains separate.
It controls whether handlers update observation flags; it does not register, unregister, show, or hide a probe.

## Your task

Edit one file:

- `interface/proc.c`

In `rk_proc_show()`, return `SEQ_SKIP` for `RK_PROBE_FILENAME_RMDIR` before any seq output.
Leave the supplied iterator, catalog, lifecycle, control grammar, and formatter unchanged.

Run:

```bash
./rootkitlings check
```

The checker covers exact output, small reads, EOF, seek and rewind, repeated opens, command rejection, live target behavior, both registry entries, the absent negative control, all inherited failure paths, concurrent teardown, reload, and stale-state cleanup.

## Official Linux v7.2 references

- [The seq_file interface and `SEQ_SKIP`](https://docs.kernel.org/7.2/filesystems/seq_file.html)
- [Linux v7.2 seq_file core](https://github.com/torvalds/linux/blob/v7.2/fs/seq_file.c)
- [Linux v7.2 Kprobe registration and rollback](https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c)
- [Linux v7.2 Kprobe declarations](https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h)
- [Linux v7.2 procfs file operations](https://github.com/torvalds/linux/blob/v7.2/fs/proc/inode.c)
- [Linux v7.2 directory-operation targets](https://github.com/torvalds/linux/blob/v7.2/fs/namei.c)
- [Kprobes API and debugfs registry](https://docs.kernel.org/7.2/trace/kprobes.html)
