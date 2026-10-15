# Hide files and PIDs from directory listings

When `ls` or `ps` runs, the kernel fills a buffer with directory entries via the
`getdents64` syscall.
Intercept that syscall, scan the buffer, and remove entries that match your
target lists.
The result: files and PIDs vanish from every tool that reads directories.

## How getdents64 works

The syscall fills a user-space buffer with variable-length records:

```
struct linux_dirent64 {
    u64    d_ino;      // offset 0:  inode number
    s64    d_off;      // offset 8:  next entry offset
    u16    d_reclen;   // offset 16: this record's length
    u8     d_type;     // offset 18: file type
    char   d_name[];   // offset 19: null-terminated name
};
```

Records are packed end-to-end.
The return value is the total byte count.
There is no count of records.
You walk the buffer by advancing by `d_reclen` until you have consumed all
returned bytes.

## The interception pattern

Use a kretprobe on `__x64_sys_getdents64`:

1. **entry_handler**: save the user-space buffer pointer and fd from the
   registers.
   Store them in the kretprobe's per-instance data.

2. **return_handler**: read the original return value (total bytes).
   If zero or negative, do nothing.
   Otherwise:
   - `copy_from_user` the entire dirent buffer into a kernel allocation.
   - Walk each record.
     Check `d_name` against file_targets and pid_targets.
   - If a record matches: `memmove` the rest of the buffer over it, subtract
     `d_reclen` from the total byte count.
   - `copy_to_user` the filtered buffer back.
   - `regs_set_return_value` with the new total.

## Two target lists

Maintain two `rk_target_list` instances:

- `file_targets`: names to hide from directory listings (e.g., "secret.txt").
- `pid_targets`: PID strings to hide from /proc (e.g., "1234").

## Command interface

Wire four mkdir commands through your command dispatcher:

- `mkdir rklings.h.f.<name>`: add name to file_targets
- `mkdir rklings.u.f.<idx>`: remove file_targets entry by index
- `mkdir rklings.h.i.<pid>`: add pid to pid_targets
- `mkdir rklings.u.i.<idx>`: remove pid_targets entry by index

## Demo

```sh
$ touch secret.txt && ls
secret.txt  other.txt
$ mkdir rklings.h.f.secret.txt
$ ls
other.txt
$ mkdir rklings.u.f.0
$ ls
secret.txt  other.txt
```

```sh
$ sleep 999 &
[1] 1234
$ ps aux | grep 1234
user  1234  0.0  sleep 999
$ mkdir rklings.h.i.1234
$ ps aux | grep 1234
(no output)
```

## Verify

```
./rklings check
```

After loading the module: hide a file with `mkdir rklings.h.f.<name>`, confirm
`ls` no longer shows it.
Hide a PID with `mkdir rklings.h.i.<pid>`, confirm `ps` no longer shows it.
Unhide both and confirm they return.
