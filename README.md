# Rootkitlings

Learn Linux kernel rootkit development one mechanism at a time.

58 lings across 9 categories. 6 [capstones](capstones/README.md) where you
stop learning and start building. One evolving source file. One disposable VM.

## Teaching pattern

Every ling follows the same rhythm:

```
  read the concept           (rootkitlings readme N)
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

Each ling teaches one concept, gives you the API, and lets you write the
code. The check tells you exactly what passed and what failed. If you are
stuck, `rootkitlings hint N` points you in the right direction without
giving the answer.

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
| 00 | [Foundation](lings/00_foundation/README.md) | 00-10 | Module lifecycle, build, multi-file, debugging, params, alloc, procfs |
| 01 | [Interfaces](lings/01_interfaces/README.md) | 11-15 | Error pointers, proc write, lists, char device, ioctl |
| 02 | [Concurrency](lings/02_concurrency/README.md) | 16-22 | Timer, workqueue, kthread, waiting, mutex, atomics |
| 03 | [Introspection](lings/03_introspection/README.md) | 23-27 | Task, credentials, escalation, symbols, randomness |
| 04 | [Hooking](lings/04_hooking/README.md) | 28-33 | Tracefs, kprobe, kretprobe, write protection, ftrace, lifecycle |
| 05 | [Hiding](lings/05_hiding/README.md) | 34-40 | Module, sysfs, getdents64, file content, log, process, port |
| 06 | [Networking](lings/06_networking/README.md) | 41-49 | Netfilter, packet sniffing, ICMP trigger, raw socket, DNS spoof, TCP backdoor, covert channel, traffic counter, packet mangling |
| 07 | [Persistence](lings/07_persistence/README.md) | 50-54 | Proc trigger, init hooks, file infection, boot persistence, self-healing |
| 08 | [Crypto](lings/08_crypto/README.md) | 55-57 | XOR obfuscation, AES encryption, hash verification |

See the complete [roadmap](lings/README.md).

## Capstones

Every ~10 lings, a [capstone](capstones/README.md) assembles what you learned into
something you can run, break, and show off. No new concepts, just assembly.

| # | Capstone | After | Builds |
|--:|----------|------:|--------|
| 0 | Message board | 10 | sysfs + procfs + heap state |
| 1 | Command box | 15 | ioctl + list + miscdevice |
| 2 | Tripwire | 33 | ftrace hook + access log |
| 3 | Dead drop | 40 | hidden /proc + signal handler |
| 4 | Remote shell | 49 | kernel C2 + beacon loop |
| 5 | Red vs blue | 57 | full rootkit + detection game |

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
./rootkitlings hint   [N]        # Get a nudge (not the answer)
./rootkitlings peek   [N]        # Show the reference solution
./rootkitlings reset  [N]        # Restore hello.c to the reference
./rootkitlings status            # Show progress and next steps
./rootkitlings list              # Show all lings
./rootkitlings run               # Boot interactive VM
./rootkitlings check capstone-N  # Verify capstone N
```

All module execution happens in the disposable [QEMU VM](vm/README.md).

## When things break

Ling 03 teaches GDB debugging, and ling 04 covers init failure handling.
Read them before your first real bug.

## Beyond rootkits

Every technique here is a kernel module primitive. The same mechanisms
power legitimate software: device drivers (procfs, chardev, ioctl),
monitoring tools (kprobes, ftrace), security modules (credential hooks,
netfilter), and filesystem features (VFS callbacks). Understanding how
rootkits abuse these interfaces is exactly what makes you effective at
defending them.

## Multi-file modules

Ling 02 teaches multi-file builds: `hello-y` in Kbuild, shared headers,
when to split. Capstone 1's reference solution demonstrates the pattern.

## Editor support

| Editor | Config |
|--------|--------|
| clangd (any editor) | `.clangd` strips kernel-specific GCC flags |
| clang-format | `.clang-format` uses kernel coding style |
| All editors | `.editorconfig` sets tabs/indent per file type |

Run `make editor` after any build to regenerate `compile_commands.json`.
