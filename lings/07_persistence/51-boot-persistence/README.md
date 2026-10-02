# 51 - Boot persistence

## Why this step

A rootkit that disappears on reboot is useless. Linux auto-loads modules
listed in `/etc/modules-load.d/*.conf` at boot. This ling writes that
config file from kernel space.

```
  insmod hello.ko
       |
       v
  +-----------------------------+
  | 1. mkdir -p /etc/modules-   |
  |    load.d/  (via UMH)       |
  |                             |
  | 2. filp_open("hello.conf") |
  |    kernel_write("hello\n")  |
  |    filp_close               |
  +-----------------------------+
       |
       v
  next reboot loads hello.ko automatically
```

## What you need

From ling 49 you know `call_usermodehelper`. This ling adds kernel-space
file I/O, the other half of persistence.

## New API

```c
#include <linux/fs.h>
```

| API | Purpose |
|-----|---------|
| `filp_open(path, flags, mode)` | Open a file from kernel space (returns `struct file *` or ERR_PTR) |
| `kernel_write(f, buf, len, &off)` | Write from kernel memory (no `copy_from_user` needed) |
| `filp_close(f, NULL)` | Close the file |

`filp_open` returns an `ERR_PTR` on failure, same pattern as ling 09.
Check with `IS_ERR()` before using the pointer.

Use `O_WRONLY | O_CREAT | O_TRUNC` flags and `0644` mode.

## Build it

1. In `init`, ensure the directory exists. Use `call_usermodehelper`
   to run `mkdir -p /etc/modules-load.d` (ling 49 pattern).
2. Open `/etc/modules-load.d/hello.conf` from kernel space with
   `filp_open`.
3. Write `"hello\n"` with `kernel_write`.
4. Close with `filp_close`.
5. `/proc/rk_boot` shows the path and content that was written.
6. In `exit`, clean up with `call_usermodehelper` running `rm -f` on
   the config file. (A real rootkit would NOT clean up.)

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_boot                         # shows persistence status
cat /etc/modules-load.d/hello.conf      # module name written here
dmesg | tail
rmmod hello
cat /etc/modules-load.d/hello.conf 2>&1 # cleaned up on exit
```

## Contract

The check script verifies:

- On load, `/etc/modules-load.d/hello.conf` is created containing `hello`.
- `/proc/rk_boot` contains `path=/etc/modules-load.d/hello.conf` and `content=hello`.
- On unload, the persistence file is removed.

## Verify

```bash
./rootkitlings check 51
```

> **What a defender sees**: a new `.conf` file in `/etc/modules-load.d/`.
> Comparing the directory contents against a known baseline catches this.
> Advanced rootkits also modify the initramfs to load even earlier.

See also: [52 - Self-healing](../52-self-healing/README.md).
