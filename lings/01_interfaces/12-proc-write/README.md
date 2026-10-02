# 12 - Procfs write crosses the boundary in the other direction

## Why this step

Ling 10 made `/proc/rk_reader` readable: the kernel copied data to userspace.
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

Use the `single_open` pattern: a `show` function writes to a
`seq_file` with `seq_printf`. Format the stored `message`
(add a trailing `\n`).

## Registration

For a read+write proc entry with seq_file, your `proc_ops` needs:

```c
static int rk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

static const struct proc_ops rk_ops = {
	.proc_open    = rk_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = rk_write,
};
```

Use `proc_create("rk_store", 0666, NULL, &rk_ops)` with mode `0666`
(read and write for everyone).

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
./rootkitlings check 12
```
> **What a defender sees**: `/proc/rk_store` is world-writable. Any user on
> the system can change the stored message. No authentication, no audit trail.

See also: [13 - Lists](../13-lists/README.md).
