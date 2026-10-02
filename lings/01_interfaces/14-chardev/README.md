# 14 - A character device moves the interface to /dev

## Why this step

Procfs entries sit in `/proc`, visible to every user with `ls`. A character
device under `/dev` offers the same file operations but with standard Unix
permissions, udev integration, and a clean separation from kernel internals.

The kernel's `miscdevice` API is the simplest way to register a character
device. It handles major/minor number allocation automatically.

## The new API surface

```
  /proc/rk_* (lings 09-12)          /dev/rk_chardev (this ling)
  +--------------------------+       +---------------------------+
  | struct proc_ops          |       | struct file_operations    |
  |   .proc_read             |       |   .owner = THIS_MODULE   |
  |   .proc_write            |       |   .open                  |
  | proc_create / proc_remove|       |   .release               |
  +--------------------------+       |   .read                   |
                                     | misc_register / misc_dereg|
                                     +---------------------------+
```

Two differences from procfs:

1. The operations table is `struct file_operations` (from `linux/fs.h`),
   not `struct proc_ops`. It has `.owner = THIS_MODULE` to prevent unload
   while the device is open.
2. Registration uses `misc_register(&dev)` / `misc_deregister(&dev)`.

## Reset the experiment

```bash
git diff -- module/hello.c
git restore module/hello.c
```

## What to build

A `/dev/rk_chardev` character device that logs opens and closes (with the
caller's PID) and returns `hello\n` on read.

You need `linux/miscdevice.h`. No `linux/proc_fs.h` this time.

## The miscdevice struct

```c
static struct miscdevice rk_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "rk_chardev",
    .fops  = &rk_fops,
};
```

`MISC_DYNAMIC_MINOR` asks the kernel to assign a minor number.
The `.name` becomes the `/dev/` filename. The `.fops` points to
your file operations table.

## Callbacks

You need three callbacks:

```
  cat /dev/rk_chardev
    |
    +-> VFS: open()    -> rk_open(inode, file)     log PID
    +-> VFS: read()    -> rk_read(file, buf, ...)  return data
    +-> VFS: release() -> rk_release(inode, file)  log PID
```

**open and release** have this signature:

```c
static int rk_open(struct inode *inode, struct file *file)
static int rk_release(struct inode *inode, struct file *file)
```

Both return `0` on success. Log the caller's PID using `current->pid`.
`current` is a macro that returns the `task_struct` of the calling process.

**read** uses the `single_open` + `seq_file` pattern. Your `open` callback
calls `single_open(file, show_fn, NULL)`, and the `file_operations` uses
`seq_read`, `seq_lseek`, and `single_release`.

## Registration

Build the operations table with `.owner = THIS_MODULE` (new) plus your
three callbacks. Then `misc_register` in init, `misc_deregister` in exit.

`misc_register` returns an error code (not a pointer), so check with
the `if (err)` pattern, not `IS_ERR`.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko
ls -la /dev/rk_chardev
cat /dev/rk_chardev
dmesg | tail
rmmod hello
ls /dev/rk_chardev
```

## Contract

The check script verifies:

- `/dev/rk_chardev` exists and is a character device after loading.
- `/sys/class/misc/rk_chardev` exists (registered as miscdevice).
- `cat /dev/rk_chardev` returns non-empty content.
- Reading works repeatedly (second read succeeds).
- `/dev/rk_chardev` and `/sys/class/misc/rk_chardev` disappear after unload.

## Verify

```bash
./rootkitlings check 14
```
> **What a defender sees**: `ls /dev/` shows the device. `lsmod` shows
> the module. The device node is the most visible control surface so far.

See also: [15 - Ioctl](../15-ioctl/README.md).
