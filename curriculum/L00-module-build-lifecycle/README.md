# Build and load a kernel module

Start with a module that can enter and leave the test kernel cleanly.
Build it, load it, confirm that it is resident, then unload it.
Success proves that you control the complete module lifecycle before adding any
new behavior.

Most programs run in **userspace**, isolated from the operating system.
A kernel module runs inside the kernel with full kernel privileges.
Drivers and loadable rootkits use this mechanism to add functionality.

A bug in a module can crash or corrupt the whole kernel.
This exercise loads code only in the disposable test VM, never in the host
kernel.

## From source to a running module

Linux cannot load a C source file directly.
The kernel build system compiles it into an object and links a loadable kernel
object, whose filename ends in `.ko`:

```text
core/main.c
     |
     | Kbuild
     v
core/main.o ----> rootkitlings.ko
                       |
                       | insmod
                       v
                 load callback
                       |
                       | rmmod
                       v
                unload callback
```

This is an **external module**: its source lives outside the kernel source tree,
but it is built against the target kernel's headers and configuration.
The build command identifies the module workspace with `M=<workspace>`.

`Kbuild` names the module and its object:

```make
obj-m += rootkitlings.o
rootkitlings-y := core/main.o
```

`obj-m` declares `rootkitlings.ko` as a loadable module.
`rootkitlings-y` lists the objects linked into it.

## Module lifecycle

A module has no `main()` function.
The kernel calls registered callbacks when it accepts and removes the module:

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

`module_init()` registers the load callback.
Returning `0` tells the loader that initialization succeeded.
A negative return value rejects the load; the module does not remain installed,
and its exit callback is not called.

`module_exit()` registers the unload callback.
It runs before the kernel releases a successfully loaded module.
After successful initialization, the kernel may discard code marked `__init`.
Every resource created during init must be removed before the exit callback
returns; otherwise the kernel could call code that is no longer loaded.

These macros add static metadata to the module:

```c
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings kernel module");
```

`modinfo` can read this metadata without loading the module.
`insmod rootkitlings.ko` asks the kernel to load the file.
`rmmod rootkitlings` asks it to remove the loaded module by name.

## Your task

Edit only `Kbuild` and `core/main.c`:

- Build `rootkitlings.ko` from `core/main.o`.
- Register a load callback that returns `0`.
- Register an unload callback.
- Set the license to `GPL`.
- Set the description to `Rootkitlings kernel module`.

The module must build without warnings.
You may add kernel log messages for debugging, but messages do not prove that a
module is loaded or cleaned up.

## Check and observe

From the repository root, run:

```bash
./rootkitlings check
```

The host phase builds the module and inspects its metadata.
The guest phase proves the lifecycle behavior by loading it and checking two
kernel-maintained views: `/proc/modules` and `/sys/module/rootkitlings`.
It then unloads the module, proves both views disappeared, and immediately
repeats the load/unload cycle.

These same artifacts are visible to defenders: the `.ko` exposes metadata and
symbols before loading, and a loaded module appears in kernel inventory and
sysfs.
Kernel logs may outlive the module, so they are useful diagnostics but not
cleanup evidence.

## Official references

- [Building external modules](https://docs.kernel.org/kbuild/modules.html)
- [Driver basics: `module_init()` and `module_exit()`][ref-1]

[ref-1]:
  https://docs.kernel.org/driver-api/basics.html#c.module_init
