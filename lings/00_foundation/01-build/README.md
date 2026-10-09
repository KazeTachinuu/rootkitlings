# 01 - Build and inspect a module

`hello.c` is source code.
Linux cannot load it directly: the kernel build system must turn it into a kernel object, `hello.ko`.

```text
hello.c  ------>  hello.o  ------>  hello.ko
 source           object            loadable module
 you edit         intermediate      insmod loads this
```

Keep `hello.c` unchanged.
This time, inspect what the build produces.

## Kbuild

The `Kbuild` file in the workspace contains one rule:

```make
obj-m += hello.o
```

`obj-m` declares a loadable module.
Kbuild takes `hello.c`, compiles `hello.o`, then links `hello.ko` with the metadata required by the current kernel.

`build` invokes the kernel build system for the active workspace:

```text
./rootkitlings build L01
  |
  v
scripts/kernel build-dir
  |
  | M=.rootkitlings/work/module-build
  v
.rootkitlings/work/module-build/hello.ko
```

`M=` identifies the external module directory.
The kernel build supplies the correct headers, configuration, compiler flags, and linking rules.

## Build it

```bash
./rootkitlings start L01
./rootkitlings build L01
```

Inspect the three stages:

```bash
file .rootkitlings/work/module-build/hello.c
file .rootkitlings/work/module-build/hello.o
file .rootkitlings/work/module-build/hello.ko
modinfo .rootkitlings/work/module-build/hello.ko
```

`file` identifies each format.
`modinfo` reads metadata such as the license, description, and target kernel version without loading the module.

```text
hello.c   C source
hello.o   relocatable ELF object
hello.ko  relocatable ELF kernel module
```

## Verify

```bash
./rootkitlings check L01
```

The check reads the built module's metadata, loads it, confirms its sections appear in `/sys/module/hello`, then unloads it and verifies both lifecycle messages.

## Visibility

A `.ko` is not a sealed executable.
Its metadata and printable strings can be inspected without loading it:

```bash
modinfo .rootkitlings/work/module-build/hello.ko
strings .rootkitlings/work/module-build/hello.ko | less
```
