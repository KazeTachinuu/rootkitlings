# 35 - File content filtering

## Why this step

Rootkits tamper with what userspace reads from files. This ling hooks
`__x64_sys_read` via ftrace to intercept reads on a target file and
replace sensitive patterns in the output buffer before it reaches
userspace.

Diamorphine and Reptile use similar hooks to hide entries from
`/proc/net/tcp`, `/etc/passwd`, and other files.

## What you already know

From ling 34, you can hide files from directory listings. This ling
takes a different approach: the file is visible, but its content is
modified in transit.

## The concept

```
  cat /tmp/rk_target
    |
    v
  __x64_sys_read(fd, buf, count)
    |
    v  (your hook intercepts here)
    |
    +---> call original sys_read
    |         |
    |         v
    |     buf = "data SECRET data"    (in userspace)
    |
    +---> copy_from_user into kbuf
    +---> strstr(kbuf, "SECRET") -> replace with "XXXXXX"
    +---> copy_to_user back
    |
    v
  userspace sees: "data XXXXXX data"
```

The file on disk is untouched. Only the read path is filtered.

## Build it

Hook `__x64_sys_read` using ftrace (same pattern as ling 30). The
wrapper:

1. Calls the original syscall first (fills the userspace buffer).
2. Gets the filename from the fd using `fdget()` and the dentry name.
3. If the file matches your target, copies the buffer into kernel
   space, replaces `SECRET` with `XXXXXX` (same length), and copies back.
4. Tracks the number of redactions performed.

You need `linux/file.h` for `fdget`/`fdput` (new to this ling).

On IBT kernels, the original callable address from kprobe is offset
by `ENDBR_SIZE` (4 bytes). Also create `/proc/rk_content` that reports
the redaction count.

## Predict

- Why hook `__x64_sys_read` instead of `vfs_read`? (hint: IBT, sendfile)
- What happens if the pattern spans two consecutive reads?
- Why use `within_module` to prevent recursion?


## Hints

<details>
<summary>Hint 1: why within_module for recursion</summary>

Your hook replaces `regs->ip` so the CPU jumps to your wrapper. The
wrapper calls `orig_sys_read(regs)`, which re-enters `__x64_sys_read`.
Ftrace fires again. Check `within_module(parent_ip, THIS_MODULE)`: if
the caller is your own module, return without redirecting. This lets
the original run unhooked.

</details>

<details>
<summary>Hint 2: getting the filename from a file descriptor</summary>

The first syscall argument (the fd) is in `regs->di`. After the
original returns, use `fdget()` to get a `struct fd`, then check:

```c
struct fd f = fdget((unsigned int)regs->di);
const char *name = fd_file(f)->f_path.dentry->d_name.name;
```

Compare `name` against your target filename. Call `fdput(f)` when done.

</details>

<details>
<summary>Hint 3: the replace pattern</summary>

After `orig_sys_read` returns, the data is in the userspace buffer at
`regs->si`. Copy it to a kernel buffer with `copy_from_user`, scan
with `strstr`, replace the matched bytes with `XXXXXX` (same length),
then `copy_to_user` back. The replacement must be the same length as
the original so the buffer layout is preserved.

</details>

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
echo "data SECRET data" > /tmp/rk_target
insmod hello.ko
dd if=/tmp/rk_target bs=4096 count=1 2>/dev/null   # SECRET replaced with XXXXXX
cat /proc/rk_content                                     # redaction count
rmmod hello
dd if=/tmp/rk_target bs=4096 count=1 2>/dev/null   # SECRET visible again
```

## Contract

The check script verifies:

- Reading `/tmp/rk_target` replaces `SECRET` with `XXXXXX` (surrounding text preserved).
- `/proc/rk_content` contains `redactions=` with a count greater than 0.
- After unload, the file reads normally again.

## Verify

```bash
./rootkitlings check 35
```

The check creates `/tmp/rk_target` containing "SECRET", reads it with
the module loaded (via `dd`, since BusyBox `cat` uses `sendfile`), and
verifies "SECRET" was replaced with "XXXXXX".

> **What a defender sees**: `strace` shows the read returning the correct
> byte count but with different content. Comparing a raw disk read with
> userspace output exposes the tampering.

See also: [36 - Log hiding](../36-log-hiding/README.md).
