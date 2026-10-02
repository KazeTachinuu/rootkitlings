# 10 - A procfs read crosses the userspace boundary
## Why this step

Ling 09 made `/proc/rk_reader` visible, but `cat` failed because procfs had no read
function to call. The interface exists, but it cannot return data.

The VFS routes a read to the callback responsible for that open file.

```
Read dispatch chain

  userspace              kernel
  +-------+    read()    +------+   lookup   +--------+   .proc_read   +------------+
  |  cat  | -----------> | VFS  | ---------> | procfs | ------------> | rk_read |
  +-------+              +------+            +--------+               +------------+
       ^                                                                    |
       |                                                                    v
       +---------------------- bytes copied to buffer <---------------------+
```

Keep the procfs entry from ling 09.

## Add the read callback

Add:

```c
#include <linux/fs.h>

static const char message[] = "hello\n";

static ssize_t rk_read(struct file *file, char __user *buffer,
			  size_t count, loff_t *position)
{
	ssize_t copied;

	copied = simple_read_from_buffer(buffer, count, position,
					 message, sizeof(message) - 1);
	/* optional: log the call to see how the VFS drives your callback */
	return copied;
}
```

The callback parameters are supplied by the VFS:

- `file` identifies this open file instance;
- `buffer` is the destination address in userspace;
- `count` is the maximum number of bytes requested;
- `position` records how far this open file has read.

The types reflect those roles: `size_t` holds a byte count, `loff_t` holds a
file offset, and signed `ssize_t` can return either a byte count or an error.

A read returns a positive byte count, `0` for end-of-file, or a negative errno.

`__user` documents that `buffer` belongs to userspace.
Kernel code must not dereference it as an ordinary kernel pointer.
`simple_read_from_buffer` performs the checked copy, respects `count`, advances
`position`, and returns `0` when no bytes remain.

`sizeof(message) - 1` excludes the C string's final zero byte.
The newline remains part of the data.

The unused `file` parameter is still required because callbacks must match the
function type expected by `struct proc_ops`.

## Register, do not call

Put the callback address in the table from ling 09:

```c
static const struct proc_ops rk_ops = {
	.proc_read = rk_read,
};
```

This assignment does not execute `rk_read`.
It tells procfs which function to invoke for a future read operation.

```
struct proc_ops rk_ops
+------------------+------------------+
| .proc_read       |   &rk_read    |  <- address stored, not called
| .proc_write      |      NULL        |
+------------------+------------------+

cat /proc/rk_reader
  -> VFS calls rk_ops.proc_read
  -> rk_read(file, buffer, count, position)
```

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko
dd if=/proc/rk_reader bs=2 2>/dev/null
dmesg | tail
cat /proc/rk_reader
cat /proc/rk_reader
rmmod hello
```

`dd` reads with a two-byte block size.
`2>/dev/null` hides only `dd`'s progress report so its data remains visible.

## Contract

The check script verifies:

- `/proc/rk_reader` exists after loading.
- `cat /proc/rk_reader` returns non-empty content.
- `/proc/rk_reader` is read-only (writes fail).
- Second read returns the same content (position resets between opens).
- `/proc/rk_reader` disappears after unload.

## Verify

```bash
./rootkitlings check 10
```

> **What a defender sees**: anyone can `cat /proc/rk_reader` and read the data. There is no
> write interface, no authentication, and the entry is visible in a directory listing.
> Category 01 (interfaces) adds input. Category 05 (hiding) removes the visibility.

This ling teaches the raw VFS callback so you understand what happens
under the hood. From ling 11 onward, use `proc_create_single` with
`seq_file` instead: it handles position tracking, buffer management,
and multi-read automatically.

Foundation complete. Next: [Capstone 0: Config store](../../../capstones/0-config-store/README.md).
Then continue from the [roadmap](../../README.md).

See also: [11 - Error pointers](../../01_interfaces/11-errptr/README.md).
