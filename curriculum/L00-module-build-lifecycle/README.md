# L00: Module Init

Before you can hide, you need to exist in the kernel.

A kernel module is code that loads into the running kernel at runtime.
No recompilation, no reboot.
`insmod` drops it in, `rmmod` pulls it out.
Drivers use this mechanism.
So do rootkits.

Unlike userspace programs, a module shares the kernel's address space and runs
with full kernel privileges.
A bug does not segfault your process.
It panics the machine.
Everything in this course runs inside a disposable QEMU guest for exactly that
reason.

## How it gets built

You write C.
The kernel build system (`Kbuild`) compiles it into a loadable kernel object
(`.ko`):

```text
  source                      runtime

  core/main.c                 insmod rklings.ko
       |                           |
       | Kbuild                    v
       v                      rk_init() --> return 0
  core/main.o                      |
       |                      module is resident
       | link                      |
       v                      rmmod rklings
  rklings.ko                       |
                                   v
                              rk_exit()
                                   |
                              module removed
```

Your `Kbuild` file tells the build system what to produce:

```make
obj-m += rklings.o
rklings-y := core/main.o
```

`obj-m` says "build rklings.ko as a loadable module."
`rklings-y` lists the object files that go into it.

## How it runs

There is no `main()`.
The kernel calls two callbacks you register:

```c
static int __init rk_init(void)
{
	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);
```

`rk_init` runs when the module loads.
Return 0 means success.
Return a negative errno and the kernel rejects the load entirely: the module is
never installed and `rk_exit` is never called.

`rk_exit` runs when the module unloads.
Everything you set up in init, you tear down here.
The kernel may discard `__init`-marked code after init returns, so exit cannot
call init functions.

Two metadata macros round it out:

```c
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
```

Without `GPL`, the kernel taints itself and some symbols become unavailable.
`modinfo rklings.ko` reads these fields without loading.

## You write

`Kbuild` (~2 lines):

- declare the module and its source

`core/main.c` (~19 lines):

- `rk_init`
- `rk_exit`
- license
- description

## API

```c
static int __init rk_init(void);     // return 0 = loaded
static void __exit rk_exit(void);    // clean up everything

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
```

## Test it

```
$ rklings run l00
root@vm:~# insmod rklings.ko
root@vm:~# grep rklings /proc/modules
rklings 12288 0 - Live 0x...
root@vm:~# ls /sys/module/rklings/
...
root@vm:~# rmmod rklings
root@vm:~# grep rklings /proc/modules
(no output)
```

## Validate

```
$ rklings validate l00
  [+] rklings.ko builds without warnings
  [+] modinfo shows LICENSE=GPL
  [+] insmod succeeds, /proc/modules lists rklings
  [+] /sys/module/rklings exists
  [+] rmmod succeeds, both entries gone
  [+] reload succeeds (clean lifecycle)
  PASS
```

## Next

Your module enters and leaves the kernel cleanly, but it has no way to fail
gracefully.
L01 adds goto cleanup chains so init can back out when an allocation fails,
freeing only what was actually created.
