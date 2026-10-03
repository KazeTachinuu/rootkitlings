# 00 - What is a kernel module?

## What we are learning

A loadable kernel module is compiled code that Linux can add to the running
kernel without rebuilding or rebooting it. Its file ends in `.ko`.

A module is not a userspace program:

- it runs in kernel space with kernel privileges;
- it has no `main`; the kernel calls registered entry points;
- it shares the kernel's memory and execution contexts;
- a bug can crash or corrupt the whole guest kernel.

Rootkits abuse the same mechanisms that drivers and filesystems use.

## What we are doing

Build `hello.c` into `hello.ko` on the host, load it in a disposable QEMU guest.

```
           host                            QEMU guest
     +--------------+              +-------------------------+
     |  hello.c     |              |                         |
     |     |        |    copy      |  insmod hello.ko        |
     |     v        |   ------>    |     |                   |
     |  hello.ko    |              |     v                   |
     +--------------+              |  hello_init() -> dmesg  |
                                   |     ...                 |
                                   |  rmmod hello            |
                                   |     |                   |
                                   |     v                   |
                                   |  hello_exit() -> dmesg  |
                                   +-------------------------+
```

The module never loads into the host kernel.

## Read the lifecycle

Open [hello.c](../../../module/hello.c) and find:

```c
static int __init hello_init(void)
static void __exit hello_exit(void)
module_init(hello_init);
module_exit(hello_exit);
```

`module_init` identifies the function used when the module loads.
`module_exit` identifies the function used when it unloads.
Their `pr_info` calls write evidence to the kernel log.

## Try it

```bash
./rootkitlings run
```

This builds the module, copies it into a disposable QEMU VM, and drops
you into a shell. Experiment freely: nothing you do here affects your host.

Inside the VM:

```sh
insmod hello.ko
lsmod
dmesg | tail
rmmod hello
lsmod
dmesg | tail
```

- `insmod` loads the module file.
- `lsmod` lists loaded modules.
- `dmesg` reads the kernel log; `tail` keeps its recent lines.
- `rmmod` removes the loaded module named `hello`.

## Contract

The check script verifies:

- `insmod hello.ko` succeeds and `hello` appears in `lsmod`.
- `dmesg` must contain `hello: loaded`.
- `rmmod hello` succeeds and `hello` disappears from `lsmod`.
- `dmesg` must contain `hello: unloaded`.

## Verify

```bash
./rootkitlings check 0
```
> **What a defender sees**: the module is visible in `lsmod` and its messages appear in `dmesg`.
> Every user on the system can see both.

See also: [01 - Build and inspect](../01-build/README.md).
