# L00: Build, Load, and Unload a Kernel Module

## Mission

A rootkit operates from inside the kernel, where it controls what the rest of
the system can see and do. The first step is getting code in.

Linux can load code into its running kernel without rebooting. That code is
called a kernel module.

Build `rklings.ko`, load it, observe that it is active, then unload it cleanly.

## What you will observe

The finished module prints one message when it enters the kernel and another
when it leaves:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# dmesg | tail -1
rklings: loaded
root@victim:~# lsmod | grep '^rklings '
rklings                12288  0
root@victim:~# rmmod rklings
root@victim:~# dmesg | tail -1
rklings: unloaded
```

Each command observes a different part of the lifecycle:

| Command  | Meaning                                               |
| -------- | ----------------------------------------------------- |
| `insmod` | Ask the kernel to insert a module from a `.ko` file.  |
| `dmesg`  | Read messages written to the kernel log.              |
| `lsmod`  | List modules currently loaded in the kernel.          |
| `rmmod`  | Ask the kernel to remove a loaded module by its name. |

`insmod` does not merely start `rklings.ko` as a process. It copies the module
into kernel memory and calls its initialization function. If that function
returns `0`, the module stays loaded until `rmmod` removes it.

```text
HOST                               DISPOSABLE VM
+-------------+                    +----------------------------+
| core/main.c │                    | running kernel             |
+------+------+                    │                            |
       | Kbuild                    | insmod                     |
       v                           │   └── rk_init()            |
+-------------+   stage            │       └── loaded           |
| rklings.ko  | -----------------> │                            |
+-------------+                    | rmmod                      |
                                   │   └── rk_exit()            |
                                   │       └── removed          |
                                   +----------------------------+
```

A mistake in a userspace program normally crashes that process. A mistake in
kernel code can crash the whole operating system, so the runner uses a
disposable QEMU guest rather than your host kernel.

## Workspace

```text
exercises/L00-kernel-module
├── core
│   └── main.c
└── Kbuild
```

Edit both files:

- `Kbuild` tells the kernel build system what to compile.
- `core/main.c` defines what happens when the module loads and unloads.

## Build the module

The kernel build system is called Kbuild. Lines beginning with `#` in `Kbuild`
are comments. Read its build path from top to bottom:

```text
core/main.c
    | compile
    v
core/main.o
    | link as rklings.o
    v
rklings.ko
```

The TODO below introduces the two Kbuild variables that declare this path. Later
exercises add more objects to the same module.

## Define its lifetime

The kernel calls one function when the module enters and another when it leaves.
These functions are called callbacks because you register them now and the
kernel calls them later. Registering a callback does not call it immediately.

In C, braces delimit a function's body and `return 0` reports success.

```text
insmod rklings.ko
        |
        v
     rk_init()
        |
        └── returns 0 ------ module remains loaded
        |
        └── returns -errno - load fails; rk_exit() is not called

rmmod rklings
        |
        v
     rk_exit()
        |
        └── release everything acquired by rk_init()
```

`__init` marks code used only during load, while `__exit` marks code used only
during unload. In the diagram, `-errno` means a negative kernel error code; L01
handles that failure path directly.

## Write to the kernel log

Kernel code cannot use the C library or `printf`; `pr_info` writes to the kernel
log, and `dmesg` reads that log from the shell. An explicit newline completes
each message.

## TODOs

```text
TODO 1  Kbuild:1:1       build rklings.ko from core/main.o
TODO 2  core/main.c:8:2  log a successful load
TODO 3  core/main.c:14:2 log a clean unload
TODO 4  core/main.c:17:1 register both lifecycle callbacks
```

### TODO 1 -- `Kbuild:1:1` -- Build the module

Create the complete build path from `core/main.c` to `rklings.ko`:

1. Add `rklings.o` to `obj-m` so Kbuild produces a loadable module.
2. Assign `core/main.o` to `rklings-y` as its only linked object.

### TODO 2 -- `core/main.c:8:2` -- Complete module entry

Complete `rk_init` in this order:

1. Log the exact message `rklings: loaded\n` with `pr_info`.
2. Return `0` to accept the load.

The message makes entry observable. The return value tells `insmod` that
initialization succeeded.

### TODO 3 -- `core/main.c:14:2` -- Complete module exit

Log the exact message `rklings: unloaded\n` with `pr_info`; `rk_exit` returns
nothing because a module cannot reject its unload at this point.

### TODO 4 -- `core/main.c:17:1` -- Register the callbacks

Connect the two functions to the module:

1. Register `rk_init` with `module_init`.
2. Register `rk_exit` with `module_exit`.

Registration gives the kernel its entry and exit points. The license and
description below the TODO are supplied metadata.

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
root@victim:~# lsmod | grep '^rklings '
rklings                12288  0
root@victim:~# ls /sys/module/rklings
coresize  holders  initsize  initstate  notes
refcnt  sections  srcversion  taint  uevent
```

The size shown by `lsmod` may vary. The important result is that the `rklings`
entry exists.

`rmmod` asks the kernel to run the exit callback and remove the module. Unload
it and check that the log and both module listings changed:

```console
root@victim:~# rmmod rklings
root@victim:~# dmesg | tail -1
rklings: unloaded
root@victim:~# lsmod | grep '^rklings '
root@victim:~# ls /sys/module/rklings
ls: cannot access '/sys/module/rklings': No such file or directory
```

An empty `grep` result is expected after unload. Load and unload it once more to
catch lifecycle state that was not cleaned up.

## Validate


```console
$ ./rklings validate L00
  [+] Normal load and unload
  PASS
```


## Next

The module can enter and leave the kernel when initialization succeeds. L01
forces initialization to fail at controlled points and makes each partial setup
clean up only the resources it already owns.
