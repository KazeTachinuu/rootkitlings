# Read live state through the status channel

The status channel says `active`, but that fixed word proves little.
Connect it to the module's real private state and make every read return one
stable record.
Success proves that userspace can inspect live state without exposing its
internal layout.

The record is:

```text
active=1
```

`struct rk_state` stays private to `core/state.c`.
Add one narrow accessor, `rk_state_is_active()`, instead of exposing the
structure layout to the procfs component.

## Pass state to the show function

Change `rk_proc_publish()` to receive the live state pointer.
Pass that pointer to `proc_create_single_data()`.
When procfs opens the file, the kernel gives the same pointer to the supplied
show function as `output->private`.

The show function reads `output->private` once, copies the accessor result into
one local `bool`, and emits one line with `seq_printf()`.
The `seq_file` helpers handle partial reads, file offsets, seeking, and end of
file; do not add a manual read callback.

No lock is needed here.
The `active` value does not change while the proc entry exists.
Synchronization belongs in the lesson where mutable shared state is introduced.

## Removal before destruction

Keep the existing teardown order: unpublish procfs, then destroy state.

In Linux v7.2, proc entry removal runs `proc_entry_rundown()`.
It prevents new callbacks, waits for callbacks already running, and releases
open procfs contexts before removal returns.
State remains valid throughout that rundown because the module destroys it
afterward.
A descriptor held open during unload does not keep the module loaded; reads
through that stale descriptor fail.

## Your task

Edit these four files:

- In `include/rootkit.h`, declare the state accessor and pass state to
  `rk_proc_publish()`.
- In `core/state.c`, return the private `active` value through the accessor.
- In `interface/proc.c`, pass state through `proc_create_single_data()` and
  print `active=1` from `output->private`.
- In `core/main.c`, give the owned state pointer to `rk_proc_publish()`.

Keep the state layout private.
Do not add a proc read callback, mutable state, or a lock.

## Check and observe

Run:

```bash
./rootkitlings check
```

The checker keeps the earlier build, failure, rollback, and unload checks.
It also verifies the exact output bytes, uneven partial reads, repeated end of
file, rewind and reread, rejected writes, reload, and unload while a proc file
descriptor is open.

## Official references

- [Linux v7.2 procfs entry creation and removal][ref-1]
- [Linux v7.2 procfs callback rundown][ref-2]
- [The seq_file interface](https://docs.kernel.org/filesystems/seq_file.html)
- [Linux v7.2 procfs API declarations][ref-3]

[ref-1]:
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/fs/proc/generic.c?h=v7.2

[ref-2]:
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/fs/proc/inode.c?h=v7.2

[ref-3]:
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/linux/proc_fs.h?h=v7.2
