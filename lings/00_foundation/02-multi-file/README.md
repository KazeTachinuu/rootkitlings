# 02 - Multi-file modules

A single `hello.c` works for small experiments. Real kernel modules split
logic across multiple files with shared headers.

## What you will build

A module that creates `/proc/rk_calc` showing `add=7 mul=12`.

The logic lives in three files:

| File | Role |
|------|------|
| `main.c` | Module init/exit, proc entry, calls calc functions |
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

## The contract

- `/proc/rk_calc` exists after `insmod`
- Reading it returns exactly `add=7 mul=12\n`
- `main.c` calls functions defined in `calc.c`
- `Kbuild` uses `hello-y` to link both object files

## What a defender sees

The multi-file structure is invisible at runtime. `modinfo`, `lsmod`, and
`/proc/modules` show the same `hello` module regardless of how many source
files built it. The split is purely a development convenience.
