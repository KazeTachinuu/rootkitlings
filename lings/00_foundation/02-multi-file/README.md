# 02 - Build one module from multiple files

A module often contains parts with different responsibilities.
Keeping a small public interface in a header lets one part use another without sharing its private implementation.

The provided module separates its lifecycle from a calculator:

| File | Responsibility |
|------|----------------|
| `main.c` | load, call the calculator, unload |
| `calc.h` | declare the calculator interface |
| `calc.c` | implement the calculator |
| `Kbuild` | describe the final module |

```text
main.c ----> main.o ----+
                        |
                        +----> hello.ko
                        |
calc.c ----> calc.o ----+
   ^
   |
calc.h declares rk_add() and rk_mul()
```

The split exists in the source tree only.
Linux still loads one module named `hello`.

## The interface

`calc.h` declares functions that another file may call:

```c
int rk_add(int a, int b);
int rk_mul(int a, int b);
```

`main.c` includes that header and calls both functions.
`calc.c` includes the same header and defines them.
The compiler can therefore check that every caller and implementation agrees on the function types.

The header guard prevents the declarations from being processed twice if multiple include paths reach `calc.h`:

```c
#ifndef CALC_H
#define CALC_H

/* declarations */

#endif
```

## Link the objects

Start from the provided files:

```bash
./rootkitlings start L02
```

`Kbuild` initially builds the module from `placeholder.o`:

```make
obj-m += hello.o
hello-y := placeholder.o
```

Replace the second line with the real component objects:

```make
hello-y := main.o calc.o
```

`hello-y` tells Kbuild to compile both C files and link their objects into `hello.ko`.

```text
obj-m     -> module to produce       -> hello.ko
hello-y   -> objects to combine      -> main.o + calc.o
```

## Verify

```bash
./rootkitlings check L02
```

The host check confirms that both source files were linked and that `rk_add` and `rk_mul` exist in `hello.ko`.
The VM then loads the module and observes the real result:

```text
rk: add=7 mul=12
```

## Source boundaries

Multiple files do not improve a module by themselves.
Create a boundary when it gives one component a clear interface or keeps its state and lifecycle together.
Kbuild erases that source layout when it links the objects into one module.
