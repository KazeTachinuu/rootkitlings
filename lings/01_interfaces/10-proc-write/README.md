# 10 - Procfs write crosses the boundary in the other direction

## Why this step

Ling 08 made `/proc/rk_store` readable: the kernel copied data to userspace.
This ling makes it writable: userspace sends data into the kernel.

Accepting userspace input is the first trust boundary crossing where
invalid data can crash the kernel. Every byte must be validated.

## The boundary

```
  userspace                           kernel module
  +------------------+                +---------------------+
  |                  |  write(fd)     |                     |
  |  echo "test"     |--------------->|  rk_write()         |
  |  > /proc/rk_store   |                |    copy_from_user   |
  +------------------+                |    store in buf     |
                                      +---------------------+
  +------------------+                +---------------------+
  |                  |  read(fd)      |                     |
  |  cat /proc/rk_store |<---------------|  rk_read()          |
  |                  |                |    return buf        |
  +------------------+                +---------------------+
```

## Reset the experiment

```bash
git diff -- module/hello.c
git restore module/hello.c
```

## What to build

A `/proc/rk_store` entry that stores a message (default: `hello`).
Reading returns the current message. Writing changes it.

You need `linux/uaccess.h` for the copy functions. This is the only
safe way to move data across the user/kernel boundary. Direct pointer
dereference from `__user` crashes or creates a security hole.

## The write callback

This is the new concept. The signature mirrors the read callback:

```c
static ssize_t rk_write(struct file *file, const char __user *buffer,
                         size_t count, loff_t *position)
```

The `__user` annotation marks `buffer` as a userspace address.

Your write callback must validate in this exact order:

```
  rk_write(buffer, count)
       |
  count >= sizeof(message)?
      / \
    yes   no
     |     |
  -EINVAL  copy_from_user(message, buffer, count)
              |
           returned non-zero?
              / \
            yes   no
             |     |
          -EFAULT  null-terminate
                      |
                   strip trailing '\n'
                      |
                   return count
```

1. **Length check first**: reject if `count >= sizeof(message)`.
   Without this, the copy overflows the buffer.
2. **copy_from_user**: returns non-zero on failure. Never use `memcpy`
   from a `__user` pointer.
3. **Null-terminate**: userspace data is raw bytes, not a C string.
4. **Strip newline**: `echo` appends `\n`, which would become part of
   the stored string.

## The read callback

You wrote this in ling 08. Same `simple_read_from_buffer` pattern,
but format the stored `message` with `snprintf` into a local buffer
(add a trailing `\n`).

## Registration

Register both callbacks in `struct proc_ops` (you know how from ling 08).
Use mode `0666` (read and write for everyone) instead of `0444`.

Init and exit follow the same `proc_create` / `proc_remove` pattern
from ling 07.

## Predict

- What happens if `copy_from_user` fails?
- What if count exceeds the buffer size?
- Why strip the trailing newline?

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko
cat /proc/rk_store
echo "phantom" > /proc/rk_store
cat /proc/rk_store
printf '%s' "specter" > /proc/rk_store
cat /proc/rk_store
rmmod hello
```

Note: `echo` adds a newline (which gets stripped), `printf '%s'` does not.
Both should produce the same result.

## Contract

The check script verifies:

- `/proc/rk_store` exists after loading.
- `cat /proc/rk_store` returns content (any initial value is valid).
- Writing `phantom` then reading returns `phantom`.
- Writing `specter` then reading returns `specter`.
- `/proc/rk_store` disappears after unload.

## Verify

```bash
./rootkitlings check 10
```

<details>
<summary>Explain the result</summary>

`copy_from_user` is the kernel's checked copy from userspace memory.
It verifies the address range belongs to the calling process and handles
page faults safely. If the address is invalid, it returns non-zero and
the write fails with `-EFAULT` instead of crashing.

The length check before the copy prevents buffer overflow.
Without it, a large write would overwrite kernel memory past the end
of `message[]`. This is the exact class of vulnerability that leads to
privilege escalation in real kernel bugs.

</details>

> **What a defender sees**: `/proc/rk_store` is world-writable. Any user on
> the system can change the stored message. No authentication, no audit trail.

See also: [11 - Lists](../11-lists/README.md).
