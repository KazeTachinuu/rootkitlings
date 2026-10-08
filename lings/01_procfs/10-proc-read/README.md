# 10 - Return bytes from procfs

A proc entry becomes readable when its operations table provides a read callback.

This module returns `hello\n` from `/proc/rk_reader` and tracks each open file's position correctly.

## Observe it

```sh
insmod hello.ko
cat /proc/rk_reader
dd if=/proc/rk_reader bs=2 2>/dev/null
cat /proc/rk_reader
```

```text
hello
hello
hello
```

`dd` requests small pieces, while each new `cat` starts with a fresh file position.

## Read boundary

```text
cat -> VFS -> entry_ops.proc_read -> simple_read_from_buffer()
                                              |
                                              v
                                    userspace receives bytes
```

The VFS supplies the destination userspace pointer, requested byte count, and current file position.

Kernel code must not dereference a `__user` pointer directly.

`simple_read_from_buffer()` performs the checked copy, respects `count`, advances `*position`, and returns zero after all source bytes have been consumed.

`sizeof(payload) - 1` sends the newline but excludes the C string's terminating null byte.

Storing `entry_read` in `.proc_read` registers the callback; it does not call it immediately.

## Implement it

In [`hello.c`](hello.c), the scaffold provides the payload, callback signature, operations table, proc entry, and cleanup.

Return `simple_read_from_buffer(buffer, count, position, payload, sizeof(payload) - 1)` from `entry_read()`.

Remove `__maybe_unused` from `payload` after the callback uses it.

## Verify

```sh
./rootkitlings check L10
```

The verification requires the exact payload on repeated opens, exercises a two-byte short read, rejects writes to the read-only entry, and confirms removal on unload.

Mode `0444` makes the content readable to every user who can access procfs.

For formatted output, later modules use `seq_file` to manage buffering and positions.

Next: [11 - Write to procfs](../11-proc-write/README.md).
