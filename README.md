# Rootkitlings

Build a Linux kernel rootkit one mechanism at a time.

The course has 31 lessons, four integration milestones, and one final challenge.
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

### Host compatibility

- **Supported:** x86-64 Linux.
- **Expected, not acceptance-tested:** x86-64 WSL2.
  Clone inside WSL into `~/rootkitlings`, not `/mnt/c`.
- **Unsupported:** native Windows, WSL1, macOS, and ARM64 hosts.
  Use an x86-64 Linux VM or remote host.

On Windows, clone with Linux Git from the WSL terminal.
Windows-mounted filesystems are slower and may alter executable scripts.
QEMU on macOS is insufficient because the build also needs Linux ELF tools and
Linux host interfaces.

## Start

```sh
make doctor
./rootkitlings setup
./rootkitlings prereq L00
./rootkitlings read L00
./rootkitlings start L00
# Edit the paths printed by start.
./rootkitlings check L00
```

- `setup` prepares the pinned kernel and verifies guest isolation.
- `prereq` shows direct exercise and concept dependencies.
- `read` explains the mechanism and task.
- `start` creates the workspace and lists editable files.
- `check` shows each build and runs its host and guest checks.

Exercise commands accept a serial such as `L19`, a stable ID such as
`netfilter-observer`, or an unambiguous prefix.
Most commands use the current exercise when no selector is given.
Run `./rootkitlings help` for the complete command list.

## Course

- **L00–L03:** module lifecycle, owned state, and procfs rollback.
- **L04–L07, M00:** bounded control and resident execution.
- **L08–L13, M01:** task identity, credentials, and syscall telemetry.
- **L14–L16, M02:** bounded view filters and independent evidence.
- **L17–L25, M03:** packets, sockets, framing, and guarded persistence.
- **L26–L30, C00:** authentication, replay defense, encryption, and integration.

Milestones combine only mechanisms taught by preceding lessons.
Use `list` for the route and `prereq` for direct dependencies.

## Recovery

`restore` and `reset` do different jobs:

```sh
./rootkitlings restore L12
./rootkitlings reset L12
./rootkitlings reset --all --yes
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
rootkitlings                       learner command
engine/                            catalog, state, and verification
vm/                                pinned guest runtime and tools
.rootkitlings/                     learner work, state, and archives
```

Learner edits belong under `.rootkitlings/`.
Files under `curriculum/` define the immutable course bundles.

See [`docs/COURSE.md`](docs/COURSE.md) for course design and
[`vm/README.md`](vm/README.md) for the guest runtime.
Maintainers use `make validate`; `make compile-db` creates clangd metadata.
