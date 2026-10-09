# Leave a reversible note in an owned file

A resident component needs to replace one small request file, then restore the
original when it leaves.
A failed step or outside change must not destroy either version.

Stage the note beside the original, exchange the names atomically, and restore
only while both files still match the identities you recorded.
Success proves that a kernel file change can be reversible without overwriting
someone else's later work.
It uses only a private test directory created by the checker:

- `/run/rootkitlings-guarded-file-transaction/request.conf`
- `/run/rootkitlings-guarded-file-transaction/.request.conf.rootkitlings`

The directory is a root-owned `tmpfs` mounted `nosuid,nodev,noexec` with mode
`0700`.
The module never accepts a path and never accesses `/etc` or a persistent host
filesystem.

## Your task

Edit one file:

- `persistence/transaction.c`

Implement the four functions declared in `include/transaction.h`.
The supplied platform layer performs fixed-path lookup, the locked VFS exchange,
guarded unlink, and sync calls.
Your code owns the transaction:

- read the original with a bounded loop;
- admit only a root-owned regular file with mode `0600`, one link, and at most
  256 binary bytes;
- reject a target that already contains the exact 13 bytes `rootkitlings\n`;
- create the fixed sibling exclusively and finish every short write;
- sync the candidate;
- reopen and compare identity, metadata, length, and bytes for both names;
- exchange the names, record that fact immediately, then sync the directory;
- publish `applied` only after the directory sync succeeds;
- on rollback, exchange only if both names still match their retained snapshots;
- sync the restored directory, verify the candidate again, unlink it, and sync
  once more.

If either fixed name differs when it is reopened and compared, suppress
rollback.
Make no further change and emit one warning instead of overwriting the state
that was found.

Both apply and restore run in module load or unload process context.
`/proc/rootkitlings` reports cached state only; reading it performs no file I/O.

Run:

```bash
./rootkitlings check
```

The checker covers binary originals of 0, 1, and 256 bytes, rejects 257 bytes,
forces every positive short-read and short-write size, injects failures at each
transaction stage, and changes both names before unload.
It also builds with `W=1` and runs the module against real files on the
disposable guest's private `tmpfs`.

`fsync` on `tmpfs` still checks call ordering and error handling, but it does
not prove survival across reboot or power loss.
Mode `0700` excludes unprivileged processes, not other root processes.
The exercise therefore assumes one owner of the private directory.
Another privileged writer can race after a comparison, so this is not a
compare-and-swap API for cooperating writers.

## References

- [Linux 7.2 `kernel_read()`, `kernel_write()`, and `vfs_fsync()`
  declarations][1]
- [Linux 7.2 locked rename helpers and `vfs_rename()`][2]
- [Linux 7.2 `RENAME_EXCHANGE` definition][3]
- [Linux 7.2 tmpfs file operations][4]
- [Linux VFS directory-locking rules][5]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/linux/fs.h
[2]: https://github.com/torvalds/linux/blob/v7.2/fs/namei.c
[3]: https://github.com/torvalds/linux/blob/v7.2/include/uapi/linux/fs.h
[4]: https://github.com/torvalds/linux/blob/v7.2/mm/shmem.c
[5]: https://docs.kernel.org/filesystems/directory-locking.html
