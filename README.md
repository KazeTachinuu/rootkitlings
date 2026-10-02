# Rootkitlings

Learn Linux kernel rootkit development one mechanism at a time.

56 lings across 9 categories. 6 [capstones](capstones/README.md) where you
stop learning and start building. One evolving source file. One disposable VM.

## Teaching pattern

Every ling follows the same rhythm:

```
  read the concept
       |
       v
  predict what will happen
       |
       v
  implement it yourself      (one file: module/hello.c)
       |
       v
  check it in a VM           (rootkitlings check N)
       |
       v
  understand the defense     ("What a defender sees")
```

Each ling teaches one concept visually, gives you the API, and lets you
write the code. Later lings build on earlier ones: if you wrote a proc
read callback in ling 08, ling 21 tells you to reuse that pattern, not
copy new code. Hints are available when you get stuck.

Each ling is self-contained: pick the concepts your rootkit needs, skip the rest.
The [simplest rootkit](lings/README.md#minimal-rootkit-5-lings) is 5 lings.
Everything else is optional hardening.

## Quick start

```bash
# Check host prerequisites
command -v make git file modinfo python3 bear busybox cpio gzip qemu-system-x86_64
test -d "/lib/modules/$(uname -r)/build"

# Read the first exercise
./rootkitlings readme 0

# Edit your solution
$EDITOR module/hello.c

# Test it in a disposable VM
./rootkitlings check 0

# See all lings and progress
./rootkitlings list
```

## Categories

| # | Category | Lings | Teaches |
|--:|----------|------:|---------|
| 00 | [Foundation](lings/00_foundation/README.md) | 00-08 | Module lifecycle, build, params, alloc, procfs |
| 01 | [Interfaces](lings/01_interfaces/README.md) | 09-13 | Error pointers, proc write, lists, char device, ioctl |
| 02 | [Concurrency](lings/02_concurrency/README.md) | 14-20 | Timer, workqueue, kthread, waiting, mutex, atomics |
| 03 | [Introspection](lings/03_introspection/README.md) | 21-25 | Task, credentials, escalation, symbols, randomness |
| 04 | [Hooking](lings/04_hooking/README.md) | 26-31 | Tracefs, kprobe, kretprobe, write protection, ftrace, lifecycle |
| 05 | [Hiding](lings/05_hiding/README.md) | 32-38 | Module, sysfs, getdents64, file content, log, process, port |
| 06 | [Networking](lings/06_networking/README.md) | 39-47 | Netfilter, packet sniffing, ICMP trigger, raw socket, DNS spoof, TCP backdoor, covert channel, traffic counter, packet mangling |
| 07 | [Persistence](lings/07_persistence/README.md) | 48-52 | Proc trigger, init hooks, file infection, boot persistence, self-healing |
| 08 | [Crypto](lings/08_crypto/README.md) | 53-55 | XOR obfuscation, AES encryption, hash verification |

See the complete [roadmap](lings/README.md).

## Capstones

Every ~10 lings, a [capstone](capstones/README.md) assembles what you learned into
something you can run, break, and show off. No new concepts, just assembly.

| # | Capstone | After | Builds |
|--:|----------|------:|--------|
| 0 | Message board | 08 | sysfs + procfs + heap state |
| 1 | Command box | 13 | ioctl + list + miscdevice |
| 2 | Tripwire | 31 | ftrace hook + access log |
| 3 | Dead drop | 38 | hidden /proc + signal handler |
| 4 | Remote shell | 47 | kernel C2 + beacon loop |
| 5 | Red vs blue | 55 | full rootkit + detection game |

## Tools

Build the current module:

```bash
make
```

Prepare clangd (requires `bear`):

```bash
make editor
```

Remove generated files:

```bash
make clean
```

## CLI

```bash
./rootkitlings readme [N]        # Read the exercise
./rootkitlings check  [N]        # Build and verify in a VM
./rootkitlings peek   [N]        # Show the reference solution
./rootkitlings reset  [N]        # Restore hello.c to the reference
./rootkitlings status            # Show progress and next steps
./rootkitlings list              # Show all lings
./rootkitlings run               # Boot interactive VM
./rootkitlings check capstone-N  # Verify capstone N
```

All module execution happens in the disposable [QEMU VM](vm/README.md).

## When things break

Ling 02 teaches debugging: dmesg, printk levels, reading a kernel oops,
and common mistakes. Read it before your first real bug.

## Multi-file modules

Ling 01 teaches multi-file builds: `hello-y` in Kbuild, shared headers,
when to split. Capstone 1's reference solution demonstrates the pattern.

## Editor support

| Editor | Config |
|--------|--------|
| clangd (any editor) | `.clangd` strips kernel-specific GCC flags |
| clang-format | `.clang-format` uses kernel coding style |
| All editors | `.editorconfig` sets tabs/indent per file type |

Run `make editor` after any build to regenerate `compile_commands.json`.
