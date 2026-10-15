# Rootkitlings

Build a Linux kernel rootkit one mechanism at a time.

27 lessons, 6 milestones, one final project.
Each exercise adds one bounded mechanism or integrates mechanisms already
practiced.
Modules run only in a disposable QEMU guest, never in the host kernel.

## Requirements

x86-64 Linux host with:

- 2 GiB RAM, 4 GiB free disk
- Python 3.11+, GCC, Clang, binutils, GNU Make, kernel build tools
- Static BusyBox, `cpio`, `gzip`, QEMU, OpenSSL, libelf-dev, libssl-dev

`make doctor` checks everything and prints package hints for Arch,
Debian/Ubuntu, and Fedora.
KVM is optional (QEMU falls back to TCG).
The course uses its pinned Linux 7.2.9 guest, not the host kernel.

## Start

```sh
make doctor
./rklings setup
./rklings prereq L00
./rklings read L00
./rklings start L00
# Edit the paths printed by start.
./rklings check L00
```

- `setup` prepares the pinned kernel and verifies guest isolation.
- `prereq` shows direct dependencies.
- `read` explains the mechanism and task.
- `start` creates the workspace and lists editable files.
- `check` builds, boots, and runs host and guest checks.

Exercise commands accept a serial (`L19`), a stable ID, or an unambiguous
prefix.
Most default to the current exercise.
Run `./rklings help` for the complete command list.

## Course

```text
L00-L04  module lifecycle, state, procfs
   |
L05-L09  hooks, kprobes, command dispatcher
   |
   +---> M00  persistence + module cloak
   |
L10-L13  target lists, concealment filters
   |
   +---> M01  full local control
   |
L14-L17  kernel sockets, threads, reverse shell
   |
   +---> M02  callback shell
   |
L18-L21  framing, AEAD, envelope, key provisioning
   |
   +---> M03  encrypted channel
   |
L22-L26  timeouts, download, protocol, auto-OPSEC
   |
   +---> M04  remote control
   +---> M05  full operations
   |
   +---> P00  complete rootkit
```

Milestones integrate the lessons above them into one operational module.
Use `list` for the full route and `prereq` for direct dependencies.

## Recovery

```sh
./rklings restore L12   # archive edits, recreate starter workspace
./rklings reset L12     # clear progress from L12 onward
./rklings reset --all --yes
```

- `restore` archives current edits and recreates that exercise's starter.
  Progress is unchanged.
- `reset` clears progress from the selected exercise onward and archives
  affected workspaces.
  Full reset requires `--all --yes`.
- `clean` removes build outputs but keeps source edits and progress.

## Repository layout

```text
course.toml                        route, concepts, tags, and tools
curriculum/<serial>-<stable-id>/   lessons and checks
rklings                            CLI
engine/                            catalog, state, and verification
vm/                                pinned guest runtime and tools
work/<serial>/                     your source code
.rklings/                          builds, state, snapshots, backups
```

Edits go in `work/`.
Files under `curriculum/` are immutable course bundles.

See [`docs/COURSE.md`](docs/COURSE.md) for course design and
[`vm/README.md`](vm/README.md) for the guest runtime.
`make validate` runs the maintainer checks.
`make compile-db` creates clangd metadata.
