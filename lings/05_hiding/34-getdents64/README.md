# 34 - Getdents64 hiding

## Why this step

`ls` calls `getdents64` to list directory entries. Hook this syscall,
filter entries by prefix, and hidden files become invisible to `ls`
while remaining accessible by direct path. This is the core technique
behind Diamorphine's file hiding.

## What you already know

From ling 30, you can hook functions with ftrace. From ling 26, you
can resolve symbols. This ling combines both to hook the getdents64
syscall handler.

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

## The redirect

This is the first ling where you redirect `regs->ip`. The ftrace
callback replaces the CPU's next instruction with your wrapper:

```
  __x64_sys_getdents64  called
       |
       v
  ftrace trampoline
       |
       v
  your_callback(ip, parent_ip, ops, fregs)
       |
       +---> is caller our module?
       |     (within_module check)
       |         |
       |        yes --> return (let original run)
       |         |
       |        no  --> regs->ip = hooked_getdents64
       |
       v
  hooked_getdents64(regs)
       |
       +---> orig_getdents64(regs)   // call original first
       +---> copy_from_user          // get dirent buffer
       +---> filter entries          // remove hidden names
       +---> copy_to_user            // send filtered result back
```

**IBT note**: on CET/IBT kernels (like ours), kprobe resolves to the
address AFTER the 4-byte `ENDBR64` instruction. To call the original
via function pointer, subtract `ENDBR_SIZE`:

```
  kprobe addr:     [ENDBR64] [prologue...]
                    ^         ^
                    |         +-- kprobe lands here
                    +-- indirect call needs this address
  orig_fn = (type)(addr - 4)
```

## Build it

Hook `__x64_sys_getdents64` via ftrace (ling 30 pattern). Your wrapper:

1. Calls the original syscall (let it fill the dirent buffer).
2. Copies the buffer from userspace (`copy_from_user`).
3. Walks entries using `d_reclen` to find each `d_name`.
4. Splices out entries whose name starts with your prefix.
5. Copies the filtered buffer back (`copy_to_user`) and returns the new total.

You need `linux/dirent.h` for `struct linux_dirent64`, and
`linux/uaccess.h` for the copy functions.

## Predict

- Can you hide `/proc/rk_dirfilter` from `ls /proc/`?
- Does `stat /proc/rk_dirfilter` still work?
- What about `find /proc -name "rk_*"`?


## Hints

<details>
<summary>Hint 1: the dirent buffer layout</summary>

`getdents64` writes a packed array of `struct linux_dirent64` into a
userspace buffer. Each entry has a different size. Walk them using
`d_reclen`:

```c
struct linux_dirent64 *d = buf;
while (offset < total) {
    d = (struct linux_dirent64 *)(buf + offset);
    // d->d_name is the filename
    offset += d->d_reclen;
}
```

</details>

<details>
<summary>Hint 2: the hook pattern (call original first)</summary>

Your ftrace callback should redirect `regs->ip` to a wrapper function.
The wrapper calls the original syscall first (to fill the dirent buffer),
then filters the result. Use `within_module(parent_ip, THIS_MODULE)` as
a recursion guard so the wrapper's own syscall does not re-enter.

</details>

<details>
<summary>Hint 3: splicing out an entry</summary>

To remove entry `d` at offset `off`, shift all bytes after it forward:

```c
int reclen = d->d_reclen;
int remaining = total - off - reclen;
memmove(buf + off, buf + off + reclen, remaining);
total -= reclen;
// do NOT advance offset (the next entry is now at the same position)
```

Copy the buffer from userspace with `copy_from_user`, filter it in
kernel space, then `copy_to_user` the result and return the new total.

</details>

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
./rootkitlings check 34
```

> **What a defender sees**: direct path access works. `cat /proc/rk_dirfilter`
> succeeds even though `ls /proc/ | grep rk_` shows nothing. A defender
> who guesses the name finds it.

See also: [35 - File content](../35-file-content/README.md).
