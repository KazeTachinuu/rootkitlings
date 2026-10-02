# 15 - Ioctl provides a typed command channel

## Why this step

Reading and writing `/proc` or `/dev` moves unstructured text. Ioctl gives
a typed command interface: distinct operations identified by number, each
with a defined argument type. This is the control channel every C2 implant
uses before networking, and every hardware driver uses to configure its device.

Ling 12 registered a miscdevice with open, release, and read.
This ling adds `unlocked_ioctl` and defines two command numbers.

## Reset the experiment

```bash
git diff -- module/hello.c
git restore module/hello.c
```

## What to build

A `/dev/rk_ioctl` character device with:
- A stored message (default: `hello`)
- Read returns the current message (via `cat /dev/rk_ioctl`)
- Two ioctl commands: SET and GET for the message
- Writing to the device also sets the message (via `printf > /dev/rk_ioctl`)

You need `linux/ioctl.h` and `linux/uaccess.h` in addition to the
miscdevice includes from ling 14.

## Command number encoding

```c
#define HELLO_SET_MSG _IOW('H', 1, char[64])
#define HELLO_GET_MSG _IOR('H', 2, char[64])
```

```
  _IOW('H', 1, char[64])

  +--------+----------+---------+--------+
  | dir(2) | size(14) | type(8) | nr(8)  |
  +--------+----------+---------+--------+
  | WRITE  |    64    |   'H'   |   1    |
  +--------+----------+---------+--------+
```

- `_IOW`: userspace writes data to the kernel (set).
- `_IOR`: userspace reads data from the kernel (get).
- The type character `'H'` avoids collisions with other drivers.
- The size is encoded so `strace` can decode the call.

## The ioctl callback

The new callback signature:

```c
static long rk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
```

`cmd` is the command number. `arg` is typically a pointer to a userspace
buffer, cast to `unsigned long`.

Structure the handler as a switch:

```
  rk_ioctl(cmd, arg)
       |
   switch (cmd)
      / |  \
     /  |   \
  SET  GET  default
   |    |      |
   v    v      v
  copy  copy  -ENOTTY
  from  to
  user  user
```

- **SET**: `copy_from_user` a `char[64]` from `(void __user *)arg`,
  null-terminate it, store in your message buffer. Same boundary
  crossing you practiced in ling 12.
- **GET**: `copy_to_user` the message buffer to `(void __user *)arg`.
- **default**: return `-ENOTTY` (the standard error for unrecognized
  ioctl commands; the name comes from terminal drivers).

## Registration

Build the `file_operations` table from ling 14, adding:

```c
.unlocked_ioctl = rk_ioctl,
```

`.unlocked_ioctl` is the modern callback. The older `.ioctl` held the
Big Kernel Lock and was removed in 2.6.36.

## The userspace tool

To test ioctl commands, you need a userspace program that calls
`ioctl(2)`. The provided `cmdtool` does this. It must define the
same `_IOW`/`_IOR` constants to generate matching command numbers.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko
cat /dev/rk_ioctl
./cmdtool set "phantom"
cat /dev/rk_ioctl
./cmdtool get
rmmod hello
```

## Contract

The check script verifies:

- `/dev/rk_ioctl` exists and is a character device.
- `cat /dev/rk_ioctl` returns content (any default is valid).
- Writing `phantom` then reading returns `phantom`.
- Writing `specter` then reading returns `specter`.
- Single-character writes work (edge case).
- `/dev/rk_ioctl` disappears after unload.

## Verify

```bash
./rootkitlings check 13
```

The check also tests that `/dev/rk_ioctl` is a character device and verifies
single-character edge cases.
> **What a defender sees**: `strace` reveals every ioctl call, including
> the command number and argument address. No obfuscation, no authentication.

Interfaces complete. Next: [Capstone 1 - Command box](../../../capstones/1-command-box/README.md).
Then continue from the [roadmap](../../README.md).

See also: [14 - Timer](../../02_concurrency/14-timer/README.md).
