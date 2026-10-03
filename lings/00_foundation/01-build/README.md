# 01 - Build and inspect the module

## Why this step

Ling 00 loaded `hello.ko`, but `./rootkitlings run` created that file for us.
Before changing kernel code, we need to know what is source, what is generated,
and which artifact the kernel actually loads.

Keep `hello.c` unchanged from ling 00.

## How Kbuild works

[module/Kbuild](../../../module/Kbuild) contains:

```make
obj-m += hello.o
```

`obj-m` asks Kbuild to produce `hello.ko` from the object named `hello.o`.

The [root Makefile](../../../Makefile) enters the running kernel's build tree and
passes the absolute `module` directory as an external module with `M=...`.
Kbuild supplies the configuration, headers, compiler flags, and module rules
for that kernel.

```
                    Kbuild pipeline
  +-----------+     +-----------+     +-----------+
  |  hello.c  | --> |  hello.o  | --> |  hello.ko |
  +-----------+     +-----------+     +-----------+
    source            object            module
    text              ELF reloc         ELF reloc
    you edit          intermediate      insmod loads
```

## Where to find kernel APIs

When a ling introduces a new function or struct, you can look it up:

```
  stuck on an API?
       |
       +---> search the kernel source
       |     https://elixir.bootlin.com/linux/latest/source
       |     type any function name, struct, or macro
       |
       +---> official docs
       |     https://docs.kernel.org
       |
       +---> headers on your machine
             /lib/modules/$(uname -r)/build/include/linux/
             grep -r 'function_name' to find the definition
```

Every include like `#include <linux/module.h>` maps to a file in that
headers directory. You can read it.

## Try it

```bash
make clean
make
file module/hello.c
file module/hello.o
file module/hello.ko
modinfo module/hello.ko
```

- `make clean` removes artifacts from an earlier build.
- `make` builds the module again.
- `file` identifies a file format.
- `modinfo` reads module metadata.

Also try reading the header your module uses:

```bash
grep -n 'pr_info' /lib/modules/$(uname -r)/build/include/linux/printk.h | head
```

## Contract

The check script verifies:

- `hello.ko` is non-trivial in size and `modinfo` can read its metadata.
- `insmod hello.ko` succeeds and `hello` appears in `lsmod`.
- `/sys/module/hello/sections` exists (valid module structure).
- `dmesg` must contain `hello: loaded` and `hello: unloaded`.

## Verify

```bash
./rootkitlings check 1
```
> **What a defender sees**: `file` identifies the format, `modinfo` reads the metadata,
> and `strings` reveals every printable constant. The `.ko` hides nothing.

See also: [02 - Multi-file modules](../02-multi-file/README.md).
