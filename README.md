# Rootkitlings

Build a Linux kernel rootkit one mechanism at a time.

The course has 26 lessons, nine milestones, and one final project.
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
- **L08–L10, M01:** task identity, credentials, and a one-use operator key.
- **L11–L12, M02:** syscall probes and an activity recorder.
- **L13–L15, M03:** bounded view filters and independent evidence.
- **L16–L19, M04:** packet parsing, observation, and redirection.
- **L20, M05, L21:** sockets, framed sessions, and DNS names.
- **M06–M07:** a reversible file swap and call-home control.
- **L22–L25, M08:** authentication, replay defense, encryption, and signaling.
- **C00:** integrate the course subsystems into one complete rootkit.

Lessons isolate mechanisms.
Milestones turn them into a small tool or a visible system behavior.
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
work/<serial>/                     learner source code
.rootkitlings/                     builds, state, snapshots, and backups
```

Learner edits belong under `work/`.
Files under `curriculum/` define the immutable course bundles.

See [`docs/COURSE.md`](docs/COURSE.md) for course design and
[`vm/README.md`](vm/README.md) for the guest runtime.
Maintainers use `make validate`; `make compile-db` creates clangd metadata.
