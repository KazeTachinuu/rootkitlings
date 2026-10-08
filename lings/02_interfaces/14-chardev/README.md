# 14 - Register a character device

A miscellaneous character device publishes file operations under `/dev` and lets the kernel allocate its minor number.

This module creates `/dev/rk_chardev` and returns `hello\n` when the device is read.

## Observe it

```sh
insmod hello.ko
ls -l /dev/rk_chardev
cat /dev/rk_chardev
test -e /sys/class/misc/rk_chardev && echo registered
rmmod hello
```

```text
hello
registered
```

The device node and misc-class entry disappear after unload.

## Registration path

```text
misc_register(&device)
          |
          v
 /sys/class/misc/rk_chardev
          |
          v
 /dev/rk_chardev -> device_ops -> single_open / seq_read
```

`MISC_DYNAMIC_MINOR` asks the misc subsystem to choose a free minor number.

The device name becomes the filename under `/dev`.

`struct file_operations` is the VFS operation table used by devices.

`.owner = THIS_MODULE` prevents module removal while an open file still refers to its callbacks.

`single_open()`, `seq_read`, `seq_lseek`, and `single_release` provide the complete read lifecycle for one formatted record.

`misc_register()` returns zero or a negative errno directly, not a pointer.

## Implement it

In [`hello.c`](hello.c), the scaffold provides the read path, operation table, and complete `struct miscdevice`.

Return `misc_register(&device)` from `device_init()`.

Call `misc_deregister(&device)` from `device_exit()`.

Remove `__maybe_unused` from `device` after both functions use it.

## Verify

```sh
./rootkitlings check L14
```

The verification requires a real character-device node, its misc-class registration, exact repeated reads, and removal of both views after unload.

The device name, sysfs class entry, open handles, and loaded module remain observable.

Next: [15 - Dispatch typed ioctl commands](../15-ioctl/README.md).
