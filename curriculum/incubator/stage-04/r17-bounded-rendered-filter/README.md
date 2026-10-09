# Replace bytes in one bounded read result

The supplied module watches the inode opened from `/tmp/rootkitlings-read-target` when the module loads.
After `vfs_read()` returns bytes from that inode, the module copies at most 512 returned bytes into its per-call buffer.
It replaces each complete, non-overlapping `SECRET` byte sequence with `XXXXXX`, then copies the result back to the reader.
The file on disk is never changed.

The replacement has the same width as the pattern.
The read result therefore keeps its original length, and later bytes do not need to move.

## Your task

Edit one file:

- `instrumentation/replace.c`

Complete `rk_replace_equal()`.
Treat its inputs as bytes, not C strings.
Return the number of replacements.

Use this scan:

- If `width` is zero or greater than `length`, return zero without changing the buffer.
- Compare only while a complete `width`-byte pattern remains.
- On a match, copy exactly `width` replacement bytes and advance by `width`.
- Otherwise, advance by one byte.
- Do not scan replacement bytes again.

Run:

```bash
./rootkitlings check
```

## What the supplied code guarantees

The pattern and replacement widths come from `sizeof(...) - 1` and are checked at build time.
Each active probe call owns a 512-byte scratch buffer.
The return handler does not allocate memory, sleep, or use string termination.

The handler uses the actual positive return value, not the requested read size.
A request for 4096 bytes is eligible if `vfs_read()` returns at most 512 bytes.
A result greater than 512 bytes is left unchanged and increments `oversize_results`.
A pattern split between two reads is not a complete pattern in either result and is not replaced.

The inode reference is retained for the module's lifetime.
Hard links and renames still identify the retained inode.
Deleting the path and creating a new file there creates a different inode, so the new file is not selected until the module is reloaded.

Copy failures increment `copy_failures` and change the read result to `-EFAULT`.
A successful copy back adds the number of replaced occurrences to `redactions`.
The counter is not changed when copying back fails.
User memory can change or become unmapped while the probed read is running, and a failed copy back may have changed part of the destination before reporting the error.
Changing the return to `-EFAULT` prevents `ksys_read()` from committing its local file-position update, but it does not undo other effects of the original read.

`/proc/rootkitlings` is read-only and reports exactly:

```text
limit=512
redactions=N
oversize_results=N
copy_failures=N
return_capacity=32
returns_missed=N
```

`returns_missed` is the Kretprobe's missed-instance count.
The probe reserves an instance before its entry handler can reject an unrelated file, so this value can include unrelated `vfs_read()` calls.

## One result path is not the file

This module changes the buffer returned through `vfs_read()`.
It is a view filter, not access control and not reliable concealment.

The checker demonstrates the boundary:

| Operation | Selected inode is filtered? |
|---|---:|
| `read` | yes |
| `pread64` | yes |
| `readv` and `preadv` | no |
| memory mapping | no |
| `sendfile`, `splice`, and `copy_file_range` | no |
| `io_uring` and in-kernel `kernel_read` | no |
| a `vfs_read` result larger than 512 bytes | no |

The examples are not an exhaustive list of Linux paths that can expose file contents.
Independent views must be checked before making a claim about what is hidden.

## Linux v7.2 references

- [`vfs_read`, `ksys_read`, and positional reads](https://github.com/torvalds/linux/blob/v7.2/fs/read_write.c)
- [Splice and sendfile paths](https://github.com/torvalds/linux/blob/v7.2/fs/splice.c)
- [File-backed fault handling used by memory mappings](https://github.com/torvalds/linux/blob/v7.2/mm/filemap.c#L3329)
- [No-fault userspace copy helpers](https://github.com/torvalds/linux/blob/v7.2/mm/maccess.c#L841-L928)
- [Kretprobe instance data and missed calls](https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h#L134-L165)
- [Kretprobe registration and teardown](https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c#L2110-L2227)
- [Kprobes API](https://docs.kernel.org/7.2/trace/kprobes.html)
- [x86-64 kernel argument decoding](https://github.com/torvalds/linux/blob/v7.2/arch/x86/include/asm/ptrace.h#L414-L431)
