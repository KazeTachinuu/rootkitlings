# GDB debugging

When `printk` is not enough, attach a debugger to the running kernel.

## Setup

The QEMU VM supports GDB debugging with the `gdb` command:

```bash
./rootkitlings gdb
```

This boots the VM paused, waiting for a debugger on `localhost:1234`.

In another terminal:

```bash
gdb vmlinux
(gdb) target remote :1234
(gdb) continue
```

## Loading module symbols

After `insmod`, find the module's text section address:

```bash
cat /sys/module/hello/sections/.text
```

Then in GDB:

```
(gdb) add-symbol-file module/hello.ko 0x<address>
```

Now you can set breakpoints on module functions by name.

## Essential commands

| Command | What it does |
|---------|-------------|
| `break rk_show` | Breakpoint on function entry |
| `continue` | Resume execution |
| `step` / `next` | Step into / step over |
| `print variable` | Show a variable's value |
| `bt` | Stack backtrace |
| `info registers` | CPU register state |

## GDB vs pwndbg

pwndbg is a GDB plugin that adds color, automatic context display, and
kernel-aware commands:

| Feature | GDB | pwndbg |
|---------|-----|--------|
| Register display | `info registers` (manual) | Automatic on every stop |
| Disassembly | `disassemble` (plain) | Color-coded, automatic |
| Memory inspection | `x/` commands | `telescope`, `hexdump` |
| Struct printing | `print *ptr` | `dt` with field layout |
| Stack trace | `bt` | Enhanced with source context |

Same connection method. pwndbg wraps GDB, it does not replace it.

## The exercise

The template has a bug. The function `compute(8, 5, 2)` should return
`base * factor + offset = 42`, but it returns 38.

1. Boot with `./rootkitlings gdb`
2. Attach GDB: `target remote :1234`
3. Load symbols, set a breakpoint on `rk_buggy_show`
4. Step through `compute()`, find the wrong operator
5. Fix it, verify `/proc/rk_debug` returns `result=42`

## The contract

- `/proc/rk_debug` exists after `insmod`
- Reading it returns exactly `result=42\n`

## What a defender sees

Debug sessions are local. No runtime artifact to detect. The fixed module
behaves identically whether debugged or not.
