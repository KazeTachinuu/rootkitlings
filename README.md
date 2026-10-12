# Rootkitlings

Build a Linux kernel rootkit one mechanism at a time.

The course has 27 lessons, seven milestones, and one final project.
Each exercise adds one bounded mechanism or integrates mechanisms already
practiced.
Modules run only in a disposable QEMU guest, never in the host kernel.

## Requirements

Use an x86-64 Linux host with:

- at least 2 GiB of memory and 4 GiB of free disk space;
- Python 3.11 or newer, GCC, Clang, binutils, GNU Make, and the kernel build
  tools;
- a statically linked BusyBox, `cpio`, `gzip`, QEMU, OpenSSL, and the libelf and
  OpenSSL development files.

`make doctor` checks these requirements and prints package hints for Arch,
Debian/Ubuntu, and Fedora.
KVM is optional; QEMU falls back to slower TCG emulation.
The course uses its pinned Linux 7.2.9 guest, not the host kernel or headers.
GDB, Bear, clangd, and ShellCheck are optional development tools.

The supported host is x86-64 Linux.

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
- `prereq` shows direct exercise and concept dependencies.
- `read` explains the mechanism and task.
- `start` creates the workspace and lists editable files.
- `check` shows each build and runs its host and guest checks.

Exercise commands accept a serial such as `L19`, a stable ID such as
`netfilter-observer`, or an unambiguous prefix.
Most commands use the current exercise when no selector is given.
Run `./rklings help` for the complete command list.

## Course

```text
L00-L03  module lifecycle, state, procfs
   |
L04-L07  control, execution engine
   |
   +---> M00  reversible persistence
   |
L08-L12  credentials, probes, telemetry
   |
   +---> M01  module cloaking
   +---> M02  local covert control
   |
L13-L15  view filters, directory hiding
   |
   +---> M03  dynamic directory filter
   |
L16-L21  packets, sockets, reverse shell
   |
   +---> M04  callback shell
   |
L22-L25  auth, replay defense, encryption
   |
   +---> M05  authenticated channel
   |
L26      port hiding
   |
   +---> M06  content and port filter
   |
   +---> C00  complete rootkit
```

Use `list` for the full route and `prereq` for direct dependencies.

## Recovery

`restore` and `reset` do different jobs:

```sh
./rklings restore L12
./rklings reset L12
./rklings reset --all --yes
```

- `restore` archives the current edits and recreates that exercise's starting
  workspace.
  Course progress is unchanged.
- `reset` clears progress from the selected exercise onward and archives the
  affected workspaces.
  Resetting the entire course requires `--all --yes`.
- `clean` removes declared build outputs but keeps source edits and progress.

## Repository layout

```text
course.toml                         route, concepts, tags, and tools
curriculum/<serial>-<stable-id>/   lessons, workspaces, and checks
rklings                       learner command
engine/                            catalog, state, and verification
vm/                                pinned guest runtime and tools
work/<serial>/                     learner source code
.rklings/                     builds, state, snapshots, and backups
```

Learner edits belong under `work/`.
Files under `curriculum/` define the immutable course bundles.

See [`docs/COURSE.md`](docs/COURSE.md) for course design and
[`vm/README.md`](vm/README.md) for the guest runtime.
Maintainers use `make validate`; `make compile-db` creates clangd metadata.
