# L00: Build, Load, and Unload a Kernel Module

This first exercise takes one small C file through the complete kernel-module
lifecycle.
You will build `rklings.ko`, load it into a disposable VM, observe it in the
running kernel, unload it, and confirm that it left cleanly.

## What you will observe

```text
HOST                               DISPOSABLE VM
+-------------+                    +----------------------------+
| core/main.c |                    | running kernel             |
+------+------+                    |                            |
       | Kbuild                    | insmod                     |
       v                           |   +-- rk_init()            |
+-------------+   stage            |       +-- resident         |
| rklings.ko  | -----------------> |                            |
+-------------+                    | rmmod                      |
                                   |   +-- rk_exit()            |
                                   |       +-- removed          |
                                   +----------------------------+
```

`rklings.ko` is a loadable kernel object.
Unlike an ordinary program, it has no `main` function and does not run as a
separate process.
Its code becomes part of the running kernel until it is unloaded.

A mistake in a userspace program normally crashes that process.
A mistake in kernel code can crash the whole operating system, so the runner
uses a disposable QEMU guest rather than your host kernel.

## Workspace

Open the two editable files:

```text
exercises/L00-module-build-lifecycle
├── Kbuild
└── core
    └── main.c
```

Read the following sections before completing the TODOs.
The runner will later build these files on the host and stage `rklings.ko` in a
guest whose kernel matches the build environment.

## Build the module

The kernel build system is called Kbuild.
This exercise needs two declarations:

```make
obj-m += rklings.o
rklings-y := core/main.o
```

Read them from top to bottom:

```text
core/main.c
    | compile
    v
core/main.o
    | link as rklings.o
    v
rklings.ko
```

`obj-m` names the loadable module. `rklings-y` lists the object files linked
into it.
Later exercises add more objects to the same list.

## Define its lifetime

The kernel calls one function when the module enters and another when it leaves:

```c
static int __init rk_init(void)
{
	pr_info("rklings: loaded\n");
	return 0;
}

static void __exit rk_exit(void)
{
	pr_info("rklings: unloaded\n");
}

module_init(rk_init);
module_exit(rk_exit);
```

`module_init` and `module_exit` register the callbacks.
They do not call them immediately.

```text
insmod rklings.ko
        |
        v
     rk_init()
        |
        +-- returns 0 ------ module remains loaded
        |
        +-- returns -errno - load fails; rk_exit() is not called

rmmod rklings
        |
        v
     rk_exit()
        |
        +-- release everything acquired by rk_init()
```

`__init` tells the kernel that it may discard the initialization code after a
successful load. `__exit` marks code used only during unload.

## Observe kernel messages

Kernel code cannot use the C library or `printf`. `pr_info` writes to the kernel
log instead:

```c
pr_info("rklings: loaded\n");
```

`dmesg` reads that log from the shell.
The explicit newline completes the message.

The module also needs metadata:

```c
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
```

`modinfo` can inspect these fields without loading the module.
The validator requires these exact values.

## Implement it

In `Kbuild`:

1. Build a module named `rklings`.
2. Link only `core/main.o` into it.

In `core/main.c`:

1. Log `rklings: loaded` and return success from `rk_init`.
2. Log `rklings: unloaded` from `rk_exit`.
3. Register both callbacks.
4. Declare the GPL license and required description.

## Test it

After completing the implementation, build it and enter the VM:

```console
$ ./rklings run L00
```

Inside the VM, load the staged module and inspect all three observable results:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# dmesg | tail -1
rklings: loaded
root@victim:~# grep '^rklings ' /proc/modules
rklings 12288 0 - Live 0x0000000000000000 (O)
root@victim:~# test -d /sys/module/rklings && echo present
present
```

The address and flags in `/proc/modules` vary by kernel.
The important result is that the `rklings` entry exists.

Unload it and prove both inventories and the lifecycle message changed:

```console
root@victim:~# rmmod rklings
root@victim:~# dmesg | tail -1
rklings: unloaded
root@victim:~# grep '^rklings ' /proc/modules
root@victim:~# test ! -e /sys/module/rklings && echo absent
absent
```

An empty `grep` result is expected after unload.
Load and unload it once more to catch lifecycle state that was not cleaned up.

## Validate

Leave the VM, then run:

```console
$ ./rklings validate L00
```

The validator checks the Kbuild composition, metadata, load and unload messages,
`/proc/modules`, `/sys/module/rklings`, and a second complete cycle.

## Next

The module can enter and leave the kernel when initialization succeeds.
L01 forces initialization to fail at controlled points and makes each partial
setup clean up only the resources it already owns.
