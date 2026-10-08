# Ghost process

This module starts a kernel thread named `khidden` and removes its PID from proc-root directory listings.
The task keeps running; only enumeration through `getdents64()` is altered.

## Hide and find it

```sh
insmod hello.ko
pid=$(cat /proc/rk_ghost)
printf 'hidden PID: %s\n' "$pid"

ps | grep khidden
ls /proc | grep "^$pid$"
```

Both searches return nothing because they enumerate `/proc`.

Direct lookup still reaches the task:

```sh
stat "/proc/$pid"
cat "/proc/$pid/comm"
kill -0 "$pid" && echo running
```

```text
khidden
running
```

The disagreement between enumeration and direct lookup is the detection technique.

## Filter path

```text
ps opens /proc
      |
      v
getdents64() entry -> save userspace buffer and verify proc root
      |
      v
kernel fills records
      |
      v
return handler -> validate records -> remove exact PID -> shorten result
```

The filter acts only on a file descriptor for the root of procfs.
Without that boundary, an unrelated directory entry whose name matches the PID could disappear.

The returned directory buffer belongs to userspace.
Copy it with `copy_from_user_nofault()`, validate every record in a private buffer, then publish the shortened result with `copy_to_user_nofault()`.
Leave malformed or concurrently changed data untouched.

## Source boundaries

```text
main.c      start components, publish PID, reverse-order rollback
ghost.c     own the khidden thread
dirhide.c   capture and filter proc-root getdents64 results
ghost.h     component interfaces
```

## Your work

Complete `filter_entries()` in `dirhide.c`.
Return immediately unless the saved directory is proc root and the syscall returned bytes.

Then:

1. Allocate a private buffer with `GFP_ATOMIC`.
2. Copy the returned bytes without faulting.
3. Validate each record's minimum size, full extent, and terminated name before using it.
4. Remove the exact `hidden_name` record with `memmove()`.
5. Copy the shortened bytes back and replace the syscall return value.
6. Free the private buffer on every path.

The thread lifecycle, proc-root check, kretprobe registration, and status entry are provided.

## Verify

```sh
./rootkitlings check C05
```

The check proves that enumeration hides the PID while direct proc access, the task name, and `kill -0` reveal it.
It also creates the same numeric filename outside procfs and proves that the filter leaves it visible.
