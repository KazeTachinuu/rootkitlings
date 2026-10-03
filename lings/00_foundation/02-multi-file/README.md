# 02 - Multi-file modules

A single `hello.c` works for small experiments. Real kernel modules split
logic across multiple files with shared headers.

## What you will build

A module assembled from two C translation units.

The logic lives in three files:

| File | Role |
|------|------|
| `main.c` | Module init/exit and calls to the calculator |
| `calc.h` | Declares `rk_add` and `rk_mul` |
| `calc.c` | Implements `rk_add` and `rk_mul` |

## Kbuild multi-file syntax

To link multiple `.o` files into one `.ko`:

```makefile
obj-m += hello.o
hello-y := main.o calc.o
```

`hello-y` tells Kbuild: "link `main.o` and `calc.o` into `hello.ko`."
The final module is still called `hello.ko`, no matter how many source
files feed into it.

```
   main.c --> main.o --+
                       +--> hello.ko
   calc.c --> calc.o --+
```

## Shared headers

`calc.h` declares the interface between files:

```c
#ifndef CALC_H
#define CALC_H

int rk_add(int a, int b);
int rk_mul(int a, int b);

#endif
```

Both `main.c` and `calc.c` include it. The header guard prevents double
inclusion.

Split a component when it owns state or resources with a clear lifecycle.
For example, hook code can expose `hook_install()` and `hook_remove()` while
keeping its private probe state in `hook.c`. Keep tightly coupled code together;
file length alone is not a reason to split it.

## The contract

- `main.c` calls functions defined in `calc.c`
- `Kbuild` uses `hello-y` to link both object files
- The result is one loadable `hello.ko`

## What a defender sees

The multi-file structure is invisible at runtime. `modinfo`, `lsmod`, and
`/proc/modules` show one `hello` module regardless of how many object files
Kbuild linked into it.

## Verify

```bash
./rootkitlings check 2
```

The host-side check inspects your files and the symbols in the linked module.
The guest-side check then loads and unloads that module. No printed string is
accepted as a substitute for the requested structure.

See also: [03 - GDB debugging](../03-gdb-debug/README.md).
