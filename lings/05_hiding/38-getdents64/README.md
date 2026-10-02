# 38 - Getdents64 hiding

## Why this step

`ls` calls `getdents64` to list directory entries. Hook this syscall,
filter entries by prefix, and hidden files become invisible to `ls`
while remaining accessible by direct path. This is the core technique
behind Diamorphine's file hiding.

## What you already know

From ling 31, you used kretprobes to intercept function returns. From
ling 34, you hooked a syscall wrapper with double indirection. This
ling combines both: a kretprobe on the syscall, with an entry handler
to capture the arguments.

## The concept

`getdents64` fills a userspace buffer with variable-length entries:

```
  ls /proc
    |
    v
  getdents64(fd, buf, count)
    |
    v
  +--------+----------+------+--------+----------+------+---
  | d_ino  | d_reclen | type | "init" | d_reclen | type | ...
  +--------+----------+------+--------+----------+------+---
  |<--- entry 1 ----->|      |<--- entry 2 ----->|

  Each entry has a DIFFERENT size (d_reclen bytes).
  d_name[] is the filename, variable-length.
```

Your hook lets the real syscall fill the buffer first,
then splices out entries you want to hide:

```
  Before filtering:
  [ init ] [ rk_dirfilter ] [ bash ] [ systemd ]

  After filtering (prefix = "rk_"):
  [ init ] [ bash ] [ systemd ]
           ^^^^^^^^^
           shifted forward to fill the gap
```

## The kretprobe entry+return pattern

This is the first ling where you use a kretprobe with an entry handler.
The problem: you need the syscall arguments (the dirent buffer pointer)
to filter the output, but by the time the return handler fires, the
original `pt_regs` argument is gone. The solution:

```
  __x64_sys_getdents64 called
       |
       v
  entry_handler(ri, regs)
       |
       +---> regs->di is a pt_regs* (syscall wrapper, ling 34)
       +---> uregs = (struct pt_regs *)regs->di
       +---> save uregs->si (dirent pointer) into ri->data
       |
       v
  original function runs, fills userspace buffer
       |
       v
  ret_handler(ri, regs)
       |
       +---> read ri->data to get saved dirent pointer
       +---> copy_from_user the filled buffer
       +---> filter entries, memmove to close gaps
       +---> copy_to_user the filtered buffer
       +---> regs_set_return_value to update byte count
```

The `ri->data` field is per-instance private storage, sized by
`.data_size` in the kretprobe struct. Define a context struct for
the data you need to pass from entry to return.

## Build it

You need:

```c
#include <linux/types.h>    // must come before dirent.h
#include <linux/dirent.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
```

Note: `linux/dirent.h` uses `u64`/`s64` without including their
definitions. Include `linux/types.h` first or the build fails.

Register a kretprobe on `__x64_sys_getdents64` with both handlers:

```c
struct getdents_ctx {
    struct linux_dirent64 __user *dirent;
};

static struct kretprobe krp = {
    .kp.symbol_name = "__x64_sys_getdents64",
    .entry_handler  = entry_getdents64,
    .handler        = ret_getdents64,
    .data_size      = sizeof(struct getdents_ctx),
    .maxactive      = 20,
};
```

The entry handler captures the dirent pointer using double indirection
(ling 34 pattern): `regs->di` is a `pt_regs*`, and the second argument
(`si`) of that inner regs is the userspace dirent buffer.

The return handler:

1. Gets the return value with `regs_return_value(regs)` (byte count).
2. Allocates a kernel buffer (`kvmalloc`) and copies the dirent data
   from userspace (`copy_from_user`).
3. Walks entries using `d_reclen` to find each `d_name`.
4. Splices out entries whose name starts with your prefix (`memmove`
   to close the gap, subtract `d_reclen` from total).
5. Copies the filtered buffer back (`copy_to_user`) and updates the
   return value with `regs_set_return_value`.

Also create `/proc/rk_dirfilter` that returns `hidden`.

## Hints

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
ls /proc/ | grep rk_          # should NOT appear
cat /proc/rk_dirfilter                 # but direct access still works
echo test > /tmp/rk_secret
ls /tmp/                        # rk_secret not listed
cat /tmp/rk_secret           # but readable directly
rmmod hello
ls /proc/ | grep rk_          # rk_ entries reappear
```

## Contract

The check script verifies:

- `/proc/rk_dirfilter` returns `hidden` via direct access.
- `ls /proc/` does NOT list `rk_dirfilter`.
- `/proc/rk_dirfilter` disappears after unload.

## Verify

```bash
./rootkitlings check 38
```

> **What a defender sees**: direct path access works. `cat /proc/rk_dirfilter`
> succeeds even though `ls /proc/ | grep rk_` shows nothing. A defender
> who guesses the name finds it.

See also: [39 - File content filtering](../39-file-content/README.md).
