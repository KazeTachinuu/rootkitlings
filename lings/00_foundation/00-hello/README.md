# 00 - Load code into the kernel

Most programs run in **userspace**, isolated from the operating system.
The Linux **kernel** manages processes, memory, devices, and network traffic.

A loadable kernel module is a `.ko` file containing code that can be added to a running kernel.
Drivers and filesystems use modules; rootkits can abuse the same mechanism.
A module runs with kernel privileges, so a bug can crash or corrupt the entire system.

## Module lifecycle

A module has no `main()`.
Linux calls one function when the module loads and another when it unloads:

```text
hello.c
   |
   | build
   v
hello.ko
   |
   | insmod
   v
hello_init() ------> "hello: loaded" in the kernel log
   |
   | rmmod
   v
hello_exit() ------> "hello: unloaded" in the kernel log
```

Open [hello.c](../../../module/hello.c):

```c
static int __init hello_init(void)
static void __exit hello_exit(void)

module_init(hello_init);
module_exit(hello_exit);
```

`module_init()` registers the load function.
Returning `0` means loading succeeded.
`module_exit()` registers the unload function.
`pr_info()` writes a message to the kernel log.

## Load and unload it

```bash
./rootkitlings run L00
```

This builds `hello.ko` and boots a disposable QEMU guest.
The module is never loaded into the host kernel.

At the guest prompt:

```sh
insmod hello.ko
lsmod
dmesg | tail

rmmod hello
lsmod
dmesg | tail
```

`insmod` loads the file.
`lsmod` shows loaded modules.
`dmesg` reads the kernel log.
`rmmod` unloads the module by name.

Expected trace:

```text
insmod hello.ko  -> hello appears in lsmod  -> hello: loaded
rmmod hello      -> hello disappears        -> hello: unloaded
```

## Verify

```bash
./rootkitlings check L00
```

The check performs the same load, observation, and unload sequence in a fresh guest.

## Visibility

While loaded, the module is visible in `lsmod` and `/sys/module/hello`.
Its `pr_info()` messages remain visible in `dmesg` after it unloads.
