# Redact one marker from a file read

A process reads a controlled file containing the marker `SECRET`.
Return `XXXXXX` in its place without changing the file on disk or altering the
read length.

Success proves that a return probe can select one file by identity and edit a
bounded read result without treating arbitrary bytes as a C string.
At load, the module retains the inode opened from
`/tmp/rootkitlings-read-target`.
It handles results of at most 512 bytes and replaces complete, non-overlapping
matches only.

Equal widths preserve the read length.

## Your task

Edit one file:

- `instrumentation/replace.c`

Complete `rk_replace_equal()`.
Treat its inputs as bytes, not C strings.
Return the number of replacements.

Use this scan:

- If `width` is zero or greater than `length`, return zero without changing the
  buffer.
- Compare only while a complete `width`-byte pattern remains.
- On a match, copy exactly `width` replacement bytes and advance by `width`.
- Otherwise, advance by one byte.
- Do not scan replacement bytes again.

Run:

```bash
./rootkitlings check
```

## What the supplied code guarantees

Build checks equal array widths, and each active call owns 512-byte scratch.
The return handler does not allocate, sleep, or use string termination.

Eligibility uses the positive return value, not the request size.
Results above 512 bytes remain unchanged and increment `oversize_results`.
A pattern split across reads is never complete in either result.

Hard links and renames retain identity; recreating the path does not select the
new inode until reload.

Copy failures increment `copy_failures` and change the read result to `-EFAULT`.
A successful copy back adds the number of replaced occurrences to `redactions`.
The counter is not changed when copying back fails.
Userspace memory may change concurrently, and failed copy-back may be partial.
`-EFAULT` blocks `ksys_read()`'s local position commit but cannot undo other
read effects.

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
The probe reserves an instance before its entry handler can reject an unrelated
file, so this value can include unrelated `vfs_read()` calls.

## One result path is not the file

This module changes the buffer returned through `vfs_read()`.
It is a view filter, not access control and not reliable concealment.

The checker demonstrates the boundary:

- Filtered: `read` and `pread64` results of at most 512 bytes.
- Not filtered: `readv`, `preadv`, memory mapping, `sendfile`, `splice`,
  `copy_file_range`, `io_uring`, and in-kernel `kernel_read`.
- Not filtered: a `vfs_read` result larger than 512 bytes.

Other paths may expose the contents; this is not reliable concealment.

## Linux v7.2 references

- [`vfs_read`, `ksys_read`, and positional reads][1]
- [Splice and sendfile paths][2]
- [File-backed fault handling used by memory mappings][3]
- [No-fault userspace copy helpers][4]
- [Kretprobe instance data and missed calls][5]
- [Kretprobe registration and teardown][6]
- [Kprobes API][7]
- [x86-64 kernel argument decoding][8]

[1]: https://github.com/torvalds/linux/blob/v7.2/fs/read_write.c
[2]: https://github.com/torvalds/linux/blob/v7.2/fs/splice.c
[3]: https://github.com/torvalds/linux/blob/v7.2/mm/filemap.c#L3329
[4]: https://github.com/torvalds/linux/blob/v7.2/mm/maccess.c#L841-L928
[5]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/kprobes.h#L134-L165
[6]: https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c#L2110-L2227
[7]: https://docs.kernel.org/7.2/trace/kprobes.html
[8]:
  https://github.com/torvalds/linux/blob/v7.2/arch/x86/include/asm/ptrace.h#L414-L431
