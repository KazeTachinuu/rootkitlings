# Stage a reversible boot-load request

A rootkit that disappears on reboot is no rootkit at all.
This milestone teaches the standard Linux persistence mechanism: writing a
`modules-load.d` configuration file so the module reloads at every boot.

## How persistence works

Linux reads `/etc/modules-load.d/*.conf` at boot.
Each line names a module to load.
Writing `rklings` to `/etc/modules-load.d/rklings.conf` makes the module survive
reboots.

In the rklings VM, init reads these files and loads the modules before dropping
you into a shell.
Test with bare mode (root shell) so you can `insmod` and `rmmod` manually:

    ./rklings run --bare

1. Boot the VM.
   No module is loaded.
2. Load the module: `insmod rklings.ko`
3. Persist it: `echo persist > /proc/rklings`
4. Reboot: `reboot`
5. The module loads automatically.
   You see `Persisted module loaded`.
6. Unload the module with `rmmod`: the transaction is reversed and the conf file
   is cleaned up.
   Next reboot starts fresh again.

## The transaction pattern

Naive persistence (just write the file) has two problems:

1. If the write is interrupted, the file is half-written.
2. If the module is unloaded, the persistence file stays behind.

The solution is a staged atomic transaction:

```text
persist:
    staging                target
    "rklings"         ""
         |                  |
         +- RENAME_EXCHANGE +
         |                  |
    ""                     "rklings"
    (old content)          (now live)

unload:
    staging                target
         |                  |
         +- RENAME_EXCHANGE +
         |                  |
    "rklings"         ""
    (swapped back)         (restored)
         |
       unlink
```

1. Create a staging file (`.rklings.conf.staging`) with O_EXCL.
2. Write the module name, fsync.
3. `RENAME_EXCHANGE` the staging file with the target.
4. fsync the directory.
5. On unload, reverse the exchange and unlink the staging file.

`RENAME_EXCHANGE` is an atomic swap: the target gets the new content and the
staging file gets the old content in a single syscall.
No window where either file is missing or half-written.

The `rk_transaction_apply()` and `rk_transaction_restore()` functions implement
this.
The platform layer handles the filesystem operations.

## The rollback guarantee

On module unload, the exit function calls `rk_transaction_restore()`, which
verifies both files still match their snapshots before exchanging them back.
If someone modified either file while the module was loaded, the restore detects
the stale state and preserves both files unchanged, logging a warning.
This prevents silent data loss.

## Control interface

The proc interface creates `/proc/rklings` (world-readable and writable):

| Command | Effect |
|---------|--------|
| `echo persist > /proc/rklings` | Stage the boot-load request |
| `cat /proc/rklings` | Show current state |

On `rmmod`, the exit path reverses the transaction automatically.

## Your task

Wire `rk_transaction_apply()` into the worker thread so it runs when the
`persist` command is received via the proc interface.
Wire `rk_transaction_restore()` into the module exit path so persistence is
rolled back on unload.

The staging, transaction, worker, and proc files are provided.
Your job is `core/main.c`: the init and exit functions that connect everything.

Run:

```sh
./rklings check
```

## References

- [Linux v7.2 `renameat2()` and `RENAME_EXCHANGE`][1]
- [Linux v7.2 `modules-load.d(5)` boot-time module loading][2]

[1]: https://github.com/torvalds/linux/blob/v7.2/fs/namei.c
[2]: https://www.freedesktop.org/software/systemd/man/latest/modules-load.d.html
