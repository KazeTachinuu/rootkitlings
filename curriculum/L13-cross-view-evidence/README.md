# Keep a live probe off the status page

A module can choose what its own status page reports.
That report may disagree with the kernel's independent record of live probes.

Omit one probe from `/proc/rootkitlings` while keeping it registered and active.
Success proves that hiding a row does not hide the underlying hook.

The module still registers the two Kprobes from the previous exercise:

- `vfs_mkdir`
- `filename_rmdir`

This exercise changes only the list rendered by `/proc/rootkitlings`.
The `filename_rmdir` probe must remain registered and active, but its
`listed_probe` row must not appear.

A fresh load must return exactly:

```text
active=1
actions=disabled
vfs_mkdir_seen=0
filename_rmdir_seen=0
listed_probe=vfs_mkdir
```

After enabling actions and creating then removing the controlled directory, both
observation flags must be one:

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

The supplied probe catalog is the source of identity for registration and
display.
Its IDs select the two actual `struct kprobe` descriptors, and `rk_probe_name()`
reads each descriptor's registered name.
Do not add another symbol-name table or compare rendered strings.

The two views have different owners:

- `/proc/rootkitlings` is this module's selected view of its catalog.
- `/sys/kernel/debug/kprobes/list` is the kernel Kprobe registry.

The checker uses the second view as independent evidence.
It also executes both target operations and checks both observation flags.
The omitted `filename_rmdir` row therefore coexists with a registered probe and
a live handler.

Registered rows and observed calls are positive controls.
The nonexistent failure symbol is a negative control and appears in neither
view.
Together they distinguish absence from a broken check.

Do not unlink `THIS_MODULE->list`; the module remains visible in `/proc/modules`
and `/sys/module`.

## How `SEQ_SKIP` changes one item

The supplied `seq_file` iterator visits exactly two catalog IDs.
Its show function receives one ID at a time.

On Linux v7.2, returning `SEQ_SKIP` tells the seq core to discard anything
emitted for that item and continue with the next item.
Make the decision before writing anything so the suppression is local and clear.
Every non-omitted ID reaches the single `listed_probe` formatter.

This is a fixed view rule.
Do not add a hide command, a second state switch, or a new failure point.
Do not remove a probe, change the catalog, mutate module or kernel lists, or
filter directories.

The action switch remains separate.
It controls whether handlers update observation flags; it does not register,
unregister, show, or hide a probe.

## Your task

Edit one file:

- `interface/proc.c`

In `rk_proc_show()`, return `SEQ_SKIP` for `RK_PROBE_FILENAME_RMDIR` before any
seq output.
Leave the supplied iterator, catalog, lifecycle, control grammar, and formatter
unchanged.

Run:

```sh
./rootkitlings check
```

The checker covers exact seq-file behavior, live probes, independent registry
evidence, rollback, teardown, reload, and stale-state cleanup.

## References

- [The seq_file interface and `SEQ_SKIP`][1]
- [Linux v7.2 seq_file core][2]
- [Linux v7.2 Kprobe registration and rollback][3]
- [Linux v7.2 Kprobe declarations][4]
- [Linux v7.2 procfs file operations][5]
- [Linux v7.2 directory-operation targets][6]
- [Linux v7.2 module-list locking and RCU updates][7]
- [Linux v7.2 `/proc/modules` iterator locking][8]
- [Kprobes API and debugfs registry][9]

[1]: https://docs.kernel.org/7.2/filesystems/seq_file.html
[2]: https://github.com/torvalds/linux/blob/v7.2/fs/seq_file.c
[3]: https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c
[4]: https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h
[5]: https://github.com/torvalds/linux/blob/v7.2/fs/proc/inode.c
[6]: https://github.com/torvalds/linux/blob/v7.2/fs/namei.c
[7]: https://github.com/torvalds/linux/blob/v7.2/kernel/module/main.c#L68-L77
[8]: https://github.com/torvalds/linux/blob/v7.2/kernel/module/procfs.c#L48-L117
[9]: https://docs.kernel.org/7.2/trace/kprobes.html
