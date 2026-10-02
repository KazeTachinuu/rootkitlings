# 03 - GDB debugging

When `printk` is not enough, attach a debugger to the running kernel.

## Setup

The QEMU VM supports GDB debugging with the `gdb` command:

```bash
./rootkitlings gdb
```

This boots the VM paused, with a GDB server on `localhost:1234` and
KASLR disabled for stable addresses.

In another terminal, load the kernel's debug symbols and connect:

```bash
gdb /usr/lib/modules/$(uname -r)/build/vmlinux
(gdb) target remote :1234
(gdb) continue
```

The kernel boots. You can `Ctrl-C` in GDB at any time to pause it.

If you use pwndbg, the same connection works. But pwndbg can misbehave
in kernel context (it expects userspace by default). If it hangs or
produces garbage, fall back to plain GDB:

```bash
gdb -nx /usr/lib/modules/$(uname -r)/build/vmlinux
```

`-nx` skips all plugins. Learn plain GDB first, add pwndbg later.

## Loading module symbols

The kernel loads your `.ko` at a runtime address. GDB needs that address
to map your module's symbols to the right memory locations.

After `insmod` in the VM, read the text section address from the VM console:

```
/ # insmod /root/hello.ko
/ # cat /sys/module/hello/sections/.text
0xffffffffc0000000
```

Copy that address, then switch to the GDB terminal:

```
(gdb) add-symbol-file module/hello.ko 0xffffffffc0000000
```

Now you can set breakpoints on module functions by name.

## Essential commands

| Command | What it does |
|---------|-------------|
| `break rk_show` | Breakpoint on function entry |
| `break compute if result > 100` | Conditional breakpoint |
| `continue` | Resume execution |
| `step` / `next` | Step into / step over |
| `finish` | Run until current function returns |
| `print variable` | Show a variable's value |
| `print *ptr` | Dereference and print a struct |
| `print/x variable` | Print in hex |
| `x/16xb addr` | Examine 16 bytes at address |
| `bt` | Stack backtrace |
| `info registers` | CPU register state |
| `watch variable` | Break when variable changes |
| `list` | Show source around current line |

## Kernel-specific tricks

**Break on module load.** Set a breakpoint on `do_init_module` to catch
any module's `init` function before it runs. Useful when you need to
step through initialization:

```
(gdb) break do_init_module
(gdb) continue
# now insmod in the VM, GDB stops before init runs
```

**Inspect current task.** If your kernel has `CONFIG_GDB_SCRIPTS`, you
get `lx-*` commands: `lx-dmesg` dumps the kernel log, `lx-lsmod` lists
loaded modules, and `lx-symbols` auto-loads module symbols on insmod.
Distribution kernels may not ship these scripts, so the manual
`add-symbol-file` workflow above always works.

**KASLR.** Kernel address space layout randomization shifts all kernel
addresses at boot. `./rootkitlings gdb` adds `nokaslr` to the kernel
command line automatically. On real systems or other VMs, you would need
to add it yourself, or GDB's symbols won't match runtime addresses.

**Print kernel structures.** GDB can print any kernel struct if symbols
are loaded:

```
(gdb) print *current
(gdb) print current->pid
(gdb) print current->comm
```

## GDB vs pwndbg

pwndbg is a GDB plugin that adds color, automatic context display, and
some kernel-aware commands:

| Feature | GDB | pwndbg |
|---------|-----|--------|
| Register display | `info registers` (manual) | Automatic on every stop |
| Disassembly | `disassemble` (plain) | Color-coded, automatic |
| Memory inspection | `x/` commands | `telescope`, `hexdump` |
| Struct printing | `print *ptr` | `dt` with field layout |
| Stack trace | `bt` | Enhanced with source context |
| Kernel tasks | manual | `kcurrent`, `kfile` |

pwndbg wraps GDB, it does not replace it. Every GDB command still works.
Recent pwndbg versions added `kcurrent` (show current task), `kfile`
(open files), `pagewalk` (page table walk), and `kmem-trace` (heap
allocation tracing). These are useful but not essential for module
debugging.

The main benefit of pwndbg for this course: automatic register and
disassembly display on every stop. You see the state without typing
`info registers` and `disassemble` after each step.

## The exercise

The template checksums the bytes `{ 'r', 'o', 'o', 't' }` and writes
the result to `/proc/rk_debug`. The expected sum is 452
(114 + 111 + 111 + 116), but the module prints something else.

The code looks correct. Read it, convince yourself, then prove yourself
wrong with GDB:

1. Boot with `./rootkitlings gdb`
2. Connect: `gdb /usr/lib/modules/$(uname -r)/build/vmlinux`, then
   `target remote :1234`, `continue`
3. In the VM, `insmod` and read the address from
   `/sys/module/hello/sections/.text`
4. In GDB: `add-symbol-file module/hello.ko 0x<address>`
5. Set a breakpoint on `checksum`, trigger it by reading `/proc/rk_debug`
6. Step through the loop. Print `sum` after each iteration. Find the
   iteration where the value stops making sense.
7. Fix the one-word bug.

## The contract

- `/proc/rk_debug` exists after `insmod`
- Reading it returns exactly `result=452\n`

## What a defender sees

Debug sessions are local. No runtime artifact to detect. The fixed module
behaves identically whether debugged or not.
