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

## Multi-file modules

One file works for learning. Real modules split into multiple files.
Kbuild supports this with `hello-y`:

```make
obj-m += hello.o
hello-y := main.o hook.o hide.o
```

```
  Kbuild compiles each .c, links them into one .ko:

  main.c ----> main.o ----+
  hook.c ----> hook.o ----+--> hello.ko
  hide.c ----> hide.o ----+
```

Share types and prototypes through a header:

```c
// hook.h
#ifndef HOOK_H
#define HOOK_H

int hook_install(void);
void hook_remove(void);

#endif
```

```c
// main.c
#include "hook.h"
```

The lings keep everything in one file so you focus on one concept.
As modules grow, split by responsibility:

| Lines | Approach |
|------:|----------|
| < 100 | Single file. Most lings. |
| 100-150 | Single file is fine, but consider splitting if you see two distinct responsibilities. |
| > 150 | Split. The ftrace hooking lings (34+) combine lookup + hook + proc into one file. In a real rootkit, those are separate files. |
| Capstones | Always split. Capstone 1's reference solution is multi-file. |

The rule: if you can describe what a file does with "and" ("it sets up
the hook AND manages the proc entry AND does the filtering"), each
"and" is a separate file.

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
             /usr/lib/modules/$(uname -r)/build/include/linux/
             grep -r 'function_name' to find the definition
```

Every include like `#include <linux/module.h>` maps to a file in that
headers directory. You can read it.

## Predict

- Which artifact is human-readable C?
- Which contains machine code but is not the final module?
- Which artifact does `insmod` accept?

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
grep -n 'pr_info' /usr/lib/modules/$(uname -r)/build/include/linux/printk.h | head
```

## Contract

The check script verifies:

- `hello.ko` is a valid ELF module (file size > 1000 bytes).
- `insmod hello.ko` succeeds and `hello` appears in `lsmod`.
- `/sys/module/hello/sections` exists (valid module structure).
- `dmesg` must contain `hello: loaded` and `hello: unloaded`.

## Verify

```bash
./rootkitlings check 1
```

<details>
<summary>Explain the result</summary>

`hello.c` and `Kbuild` are maintained inputs.
`hello.o` is an intermediate object and `hello.ko` is the loadable result.
Kbuild also creates bookkeeping files; you do not edit them.

`file` calls both compiled artifacts "relocatable."
That detail matters later when we study runtime addresses.
For now, the practical distinction is that `insmod` loads `hello.ko`, not
`hello.o`.

</details>

> **What a defender sees**: `file` identifies the format, `modinfo` reads the metadata,
> and `strings` reveals every printable constant. The `.ko` hides nothing.

See also: [02 - Init failure](../02-init-failure/README.md).
