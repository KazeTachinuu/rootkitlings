# 03 - Find a bug with GDB

The module adds the bytes in `root` and publishes the result through `/proc/rk_debug`.

```text
'r' + 'o' + 'o' + 't'
114 + 111 + 111 + 116 = 452
```

The module returns `196`.
Use GDB to find where the value changes.

## How the wrapper works

GDB runs on the host and controls the guest CPU through QEMU.

```text
terminal 1                                      terminal 2
+-----------------------------------+           +-------------------------+
| qemu-system-x86_64                |           | pwndbg vmlinux          |
|   -s                             |<-- TCP --->| target remote :1234     |
|   -S                             |   1234     | continue                |
|   -append "... nokaslr"           |           |                         |
+----------------+------------------+           +-------------------------+
                 |
                 v
          guest starts running
```

```text
-s       open the GDB server on port 1234
-S       wait for GDB before running the CPU
nokaslr  make vmlinux addresses match the running kernel
```

`-s` is shorthand for `-gdb tcp::1234`.

```text
without -S:  boot starts -> early breakpoint passes -> GDB connects
with -S:     CPU paused  -> GDB connects           -> continue -> boot
```

Two files provide symbols:

```text
vmlinux   kernel symbols
hello.ko  module symbols, available after insmod
```

`hello.ko` is relocatable.
Its symbols contain offsets, not the final addresses chosen by the kernel.

```text
checksum offset in hello.ko
              + runtime .text address from sysfs
              ----------------------------------
                guest address used by GDB
```

`add-symbol-file` relocates those symbols inside GDB.
It does not load the module into the guest.
`nokaslr` aligns `vmlinux`; `add-symbol-file` handles the separately loaded module.

The module uses `-Og -g` so GDB can follow its source and local variables.

## 1. Prepare the exercise

```bash
./rootkitlings start L03
```

This stages the deliberately faulty [`hello.c`](hello.c) for the debugger exercise.

## 2. Start QEMU — terminal 1

```bash
./rootkitlings gdb L03
```

QEMU opens port `1234` and waits.
The guest has not started yet.

## 3. Connect — terminal 2

Open the matching kernel image:

```bash
pwndbg /lib/modules/$(uname -r)/build/vmlinux
```

Use plain GDB if Pwndbg is unavailable:

```bash
gdb /lib/modules/$(uname -r)/build/vmlinux
```

Connect, then release the paused CPU:

```text
target remote :1234
continue
```

`target remote` connects to QEMU's GDB stub.
Symbol files remain on the host; QEMU provides guest registers, memory, and execution control.

Terminal 1 finishes booting and opens the guest shell.

## 4. Load the module — terminal 1

```sh
insmod hello.ko
cat /sys/module/hello/sections/.text
```

Keep the address printed by the second command:

```text
0xffffffffc0000000
```

Your address may differ.

## 5. Load the module symbols — terminal 2

GDB has no prompt while the guest runs.
Press `Ctrl-C` to pause the guest and recover it:

```text
^C
```

```text
Ctrl-C -> remote interrupt -> QEMU stops the guest CPU -> prompt returns
```

Load `hello.ko` at the address printed in terminal 1:

```text
add-symbol-file lings/00_foundation/03-gdb-debug/hello.ko 0xffffffffc0000000
break checksum
continue
```

Replace the example address with yours.

```text
add-symbol-file  relocate module symbols in GDB
break checksum   resolve checksum to a guest address and set a breakpoint
continue         let QEMU run the guest CPU
```

## 6. Trigger the bug — terminal 1

```sh
cat /proc/rk_debug
```

The read blocks because GDB stopped inside `checksum()`.

## 7. Inspect the call — terminal 2

```text
list
backtrace
frame 0
info args
```

```text
list        show the source around the current line
backtrace   show the calls that reached checksum()
frame 0     select checksum()'s stack frame
info args   show data and len
```

`bt` is the short form of `backtrace`.

Pwndbg can show the current source, registers, disassembly, and stack together:

```text
context
```

Inspect the four input bytes:

```text
hexdump data 4
```

The plain-GDB equivalent is:

```text
x/4ub data
```

`x/4ub` means examine four unsigned bytes starting at `data`.

## 8. Follow the accumulator — terminal 2

Use `next` until GDB reaches the `for` loop.
Then track the loop index and accumulator:

```text
next
info locals
display /d i
display /d sum
next
```

Repeat `next` through the loop.
The displays refresh after every stop.

```text
next -> run to the next source line -> stop -> refresh displays
```

```text
byte added      mathematical total      value stored in u8
----------      ------------------      ------------------
114             114                     114
111             225                     225
111             336                      80
116             452                     196
```

`u8` holds only `0` through `255`.
The value wraps when the total reaches `336`.

## 9. Finish and shut down

Leave the function, remove the temporary debugger state, and resume the guest:

```text
finish
undisplay
clear checksum
continue
```

Terminal 1 now prints:

```text
result=196
```

Stop the guest cleanly:

```sh
rmmod hello
poweroff
```

After QEMU exits, close the debugger:

```text
quit
```

## Fix the bug

Change the accumulator to a type that can hold `452`:

```c
u32 sum = 0;
```

## Verify

```bash
./rootkitlings check L03
```

The verification requires the real proc result:

```text
result=452
```

It also confirms that `hello.ko` contains the `checksum` symbol and GDB debug information.
Only the disposable guest kernel is debugged; the host kernel is never attached.
