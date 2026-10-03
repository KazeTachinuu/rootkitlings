# 39 - File content filtering

## Why this step

Rootkits tamper with what userspace reads from files. This ling hooks
`__x64_sys_read` via kretprobe to intercept reads on a target file and
replace sensitive patterns in the output buffer before it reaches
userspace.

Diamorphine and Reptile use similar hooks to hide entries from
`/proc/net/tcp`, `/etc/passwd`, and other files.

## What you already know

From ling 38, you used a kretprobe with entry+return handlers to
capture syscall arguments and modify output. This ling applies the
same pattern to the read syscall.

## The concept

```
  cat /tmp/rk_target
    |
    v
  __x64_sys_read(fd, buf, count)
    |
    v  (kretprobe intercepts)
    |
    entry_handler:
    +---> save fd (uregs->di) and buf (uregs->si) into ri->data
    |
    original sys_read runs, fills buf
    |
    ret_handler:
    +---> check if fd points to target file
    +---> copy_from_user_nofault into kbuf
    +---> strstr(kbuf, "SECRET") -> replace with "XXXXXX"
    +---> copy_to_user_nofault back
    |
    v
  userspace sees: "data XXXXXX data"
```

The file on disk is untouched. Only the read path is filtered.

## Build it

Hook `__x64_sys_read` with a kretprobe (same entry+return pattern as
ling 38). Your context struct stores `fd` and `ubuf` from the entry
handler.

Allocate callback scratch space with `GFP_ATOMIC` and use no-fault user-copy
helpers. On failure, preserve the original read result.

The return handler:

1. Checks if the fd matches your target file. Use `fdget()` to get
   the `struct file`, then compare
   `fd_file(f)->f_path.dentry->d_name.name` against your target name.
   Call `fdput()` when done.
2. If it matches, copies the buffer from userspace, replaces `SECRET`
   with `XXXXXX` (same length, so byte count stays the same), and
   copies back.
3. Tracks the number of redactions performed.

You need `linux/file.h` for `fdget`/`fdput` (new to this ling) and
`linux/fdtable.h` for `fd_file()`.

Also create `/proc/rk_content` that reports `redactions=N`.

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
./rootkitlings check 39
```

The check creates `/tmp/rk_target` containing "SECRET", reads it with
the module loaded (via `dd`, since BusyBox `cat` uses `sendfile`), and
verifies "SECRET" was replaced with "XXXXXX".

> **What a defender sees**: `strace` shows the read returning the correct
> byte count but with different content. Comparing a raw disk read with
> userspace output exposes the tampering.

See also: [40 - Log hiding](../40-log-hiding/README.md).
