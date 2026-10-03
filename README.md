# Rootkitlings

Learn Linux kernel rootkit development one mechanism at a time.

60 lings across 9 categories. 6 [capstones](capstones/README.md) where you
stop learning and start building. One evolving source file. One disposable VM.

Start at ling 00. It introduces each command, C construct, kernel term, build
artifact, and debugging tool before using it. On the host, `make doctor` checks
the required toolchain before you write code.

## Teaching pattern

Every ling follows the same rhythm:

```text
+--------------------------+
| 1. Learn one mechanism   |  ./rootkitlings readme N
+------------+-------------+
             |
             v
+--------------------------+
| 2. Implement it          |  edit module/hello.c
+------------+-------------+
             |
             v
+--------------------------+
| 3. Prove it in a VM      |  ./rootkitlings check N
+------------+-------------+
             |
             v
+--------------------------+
| 4. Read the evidence     |  behavior, cleanup, defense
+--------------------------+
```

Each lesson answers the same six questions:

1. What problem are we solving?
2. What happens between userspace and the kernel?
3. Which minimal API expresses that behavior?
4. Who owns each resource, and when is it released?
5. What observable behavior proves it works?
6. What can a defender see?

Each ling teaches one concept, gives you the API, and lets you write the
code. The check tells you exactly what passed and what failed. If you are
stuck, `rootkitlings hint N` points you in the right direction without
giving the answer.

Each ling is self-contained: pick the concepts your rootkit needs, skip the rest.
The [simplest rootkit](lings/README.md#minimal-rootkit-5-lings) is 5 lings.
Everything else is optional hardening.

## Requirements

- x86-64 Linux 7.2.x with its matching kernel image and build headers
- a statically linked BusyBox
- QEMU, a C toolchain, kmod, cpio, gzip, and Python 3
- KVM is optional; QEMU falls back to software emulation

`make doctor` checks the exact running kernel, required kernel features,
module policy, BusyBox applets, static linking, QEMU networking, driver
dependencies, and compression tools. Its package hints follow Arch,
Debian/Ubuntu, or Fedora/RHEL naming.

## Quick start

```bash
# Check the machine and get precise installation hints
make doctor

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

```text
+----------------------+     +---------------------------------------------+
| FOUNDATION     00-10 |     | lifecycle, build, memory, procfs            |
+----------+-----------+     +---------------------------------------------+
           |
+----------v-----------+     +---------------------------------------------+
| INTERFACES     11-16 |---->| CAPSTONE 0  CONFIG STORE                    |
|                      |     | procfs + owned state + command execution    |
+----------+-----------+     +---------------------------------------------+
           |
+----------v-----------+     +---------------------------------------------+
| CONCURRENCY    17-23 |---->| CAPSTONE 1  BEACON                          |
|                      |     | kthread + waiting + synchronized state      |
+----------+-----------+     +---------------------------------------------+
           |
+----------v-----------+
| INTROSPECTION  24-28 |     task, credentials, symbols, randomness
+----------+-----------+
           |
+----------v-----------+     +---------------------------------------------+
| HOOKING        29-35 |---->| CAPSTONE 2  TRIPWIRE                        |
|                      |     | probes + file-access evidence               |
+----------+-----------+     +---------------------------------------------+
           |
+----------v-----------+     +---------------------------------------------+
| HIDING         36-42 |---->| CAPSTONE 3  DEAD DROP                       |
|                      |     | hidden listing + direct-path leak + trigger |
+----------+-----------+     +---------------------------------------------+
           |
+----------v-----------+     +---------------------------------------------+
| NETWORKING     43-51 |---->| CAPSTONE 4  IMPLANT                         |
|                      |     | TCP session + worker + command results      |
+----------+-----------+     +---------------------------------------------+
           |
+----------v-----------+
| PERSISTENCE    52-56 |     triggers, file artifacts, watchdog
+----------+-----------+
           |
+----------v-----------+     +---------------------------------------------+
| CRYPTO         57-59 |---->| CAPSTONE 5  RED VS BLUE                     |
|                      |     | concealment + independent detection views   |
+----------------------+     +---------------------------------------------+
```

[Foundation](lings/00_foundation/README.md) / [Interfaces](lings/01_interfaces/README.md) /
[Concurrency](lings/02_concurrency/README.md) / [Introspection](lings/03_introspection/README.md) /
[Hooking](lings/04_hooking/README.md) / [Hiding](lings/05_hiding/README.md) /
[Networking](lings/06_networking/README.md) / [Persistence](lings/07_persistence/README.md) /
[Crypto](lings/08_crypto/README.md)

See the complete [roadmap](lings/README.md).

Each [capstone](capstones/README.md) uses only mechanisms introduced above it.

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

Validate the catalog, scripts, and every reference module against the current
kernel headers:

```bash
make validate
```

Run every reference solution through its behavioral check in a fresh two-vCPU
QEMU guest:

```bash
make test-references
```

## CLI

```bash
./rootkitlings readme [N]        # Read the exercise
./rootkitlings check  [N]        # Build and verify in a VM
./rootkitlings hint   [N]        # Get a nudge (not the answer)
./rootkitlings peek   [N]        # Show the reference solution
./rootkitlings status            # Show progress and next steps
./rootkitlings list              # Show all lings
./rootkitlings run               # Boot a clean interactive VM
./rootkitlings run --network     # Add networking for lings 43-51
./rootkitlings check capstone-N  # Verify capstone N
```

All module execution happens in the disposable [QEMU VM](vm/README.md).

The CLI tracks progress and wraps the VM, but it is optional. Each ling is a
self-contained directory under [`lings/`](lings/) with its own README, check
script, and hint. Browse them directly, run checks with `vm/boot`, or use any
workflow that fits.

Progress is stored in `.progress` (a single number: the index into `.order`).
It advances when a check passes. Delete it to start over, edit it to jump
ahead. It is gitignored.

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
and when splitting a module makes the code clearer.

## Editor support

| Editor | Config |
|--------|--------|
| clangd (any editor) | `.clangd` strips kernel-specific GCC flags |
| clang-format | `.clang-format` uses kernel coding style |
| All editors | `.editorconfig` sets tabs/indent per file type |

Run `make editor` after any build to regenerate `compile_commands.json`.
