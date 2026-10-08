# 15 - Dispatch typed ioctl commands

Reading a device returns one stream of bytes.

An ioctl interface adds distinct operations identified by command numbers and gives each operation a fixed argument shape.

This device supports one command that copies a message into the kernel and another that copies it back out.

## Observe it

Load the module and read the initial message:

```sh
insmod hello.ko
cat /dev/rk_ioctl
```

```text
hello
```

Use the supplied userspace helper to perform an ioctl round trip:

```sh
ioctl_test set specter
ioctl_test get
cat /dev/rk_ioctl
```

```text
specter
specter
```

The helper and module define the same command numbers, so the kernel can distinguish SET from GET.

## Command encoding

```c
#define CMD_SET_MSG _IOW('H', 1, char[MESSAGE_SIZE])
#define CMD_GET_MSG _IOR('H', 2, char[MESSAGE_SIZE])
```

`_IOW` means userspace writes the declared argument to the kernel.

`_IOR` means userspace reads the declared argument from the kernel.

The type byte `'H'`, command number, direction, and argument size are encoded into `cmd`.

```text
ioctl(fd, cmd, buffer)
          |
          v
   device_ioctl()
      /      \
     v        v
   SET        GET
copy from   copy to
 userspace  userspace
```

## Userspace pointers

The callback receives the third ioctl argument as an `unsigned long`:

```c
static long device_ioctl(struct file *file, unsigned int command,
			 unsigned long argument)
```

Cast `argument` to `void __user *` only when passing it to `copy_from_user()` or `copy_to_user()`.

Never dereference that address directly in kernel code.

SET copies into a local fixed-size array, forces its final byte to `\0`, and then stores the complete array.

GET copies the stored fixed-size array to userspace.

An unknown command returns `-ENOTTY`, the standard ioctl error for an unsupported operation.

## Implement it

Complete `device_ioctl()` in `hello.c`.

For `CMD_SET_MSG`, declare a local `char input[MESSAGE_SIZE]`, copy the full array from userspace, terminate it, then commit it under `message_lock`.

For `CMD_GET_MSG`, snapshot `message` under the lock and copy that snapshot to userspace after unlocking.

Never expose the shared array while another operation may be replacing it.

Return `-EFAULT` when either copy reports bytes it could not transfer.

Return `-ENOTTY` for every other command.

The misc device, locked read path, stored message, and ioctl command definitions are already provided.
`compat_ptr_ioctl` forwards the same fixed-width buffer ABI for 32-bit callers on a 64-bit kernel.

## Verify

```sh
./rootkitlings start L15
$EDITOR .rootkitlings/work/ioctl-dispatch/hello.c
./rootkitlings check L15
```

The verification opens the real character device, checks its initial value, performs SET and GET through `ioctl(2)`, reads the updated value through the device, and confirms cleanup after unload.

Ioctl command numbers and arguments remain visible to tracing and device inspection.

Next: [16 - Run a userspace program](../16-usermode-exec/README.md).
