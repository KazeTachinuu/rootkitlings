# Rootkitlings

Build a Linux kernel rootkit one mechanism at a time.

The course has 31 lessons (`L00`–`L30`), four integration milestones (`M00`–`M03`), and one final challenge (`C00`).
Each exercise adds one bounded mechanism or combines mechanisms already completed.
Modules are built against the course kernel and loaded only in a disposable QEMU guest, never into the host kernel.

## Requirements

Use an x86-64 Linux host with:

- at least 2 GiB of memory and 4 GiB of free disk space;
- Python 3.11 or newer, GCC, Clang, binutils, GNU Make, and the kernel build tools;
- a statically linked BusyBox, `cpio`, `gzip`, QEMU, OpenSSL, and the libelf
  and OpenSSL development files.

`make doctor` checks the exact tools, BusyBox applets, static-link support, and capacity.
It prints package hints for Arch, Debian/Ubuntu, and Fedora.
KVM is optional; QEMU uses slower TCG emulation when KVM is not available.
GDB is needed only for `./rootkitlings gdb`.
Bear and clangd are needed only for editor metadata from `make compile-db`.
ShellCheck adds shell diagnostics to `make validate` but is not required to run the course.
The host kernel version and headers are not used.
Every exercise uses the pinned Linux 7.2.9 x86-64 guest defined in [`vm/kernel.toml`](vm/kernel.toml).

### Host compatibility

| Host | Support | Use |
|---|---|---|
| x86-64 Linux | Supported | Clone normally and follow the start commands below. |
| x86-64 Windows with WSL2 | Expected compatible; not yet acceptance-tested | Clone from inside WSL into `~/rootkitlings`; do not use `/mnt/c`. KVM is optional and TCG works without nested virtualization. |
| Native Windows or WSL1 | Unsupported | Install an x86-64 WSL2 distribution. |
| macOS | Unsupported | Use an x86-64 Linux VM or remote Linux host. Apple Silicon users should prefer a remote x86-64 host. |
| ARM64 Linux or Windows on ARM | Unsupported | The course teaches x86-64 kernel mechanisms and ABI details. |

On Windows, open the WSL terminal and run `git clone` there.
Do not clone with Windows Git and then open the checkout through WSL: the Windows-mounted filesystem is substantially slower for kernel builds and may alter executable scripts.
The repository enforces LF line endings, but the Linux filesystem and executable modes are still required.

QEMU itself running on macOS is not enough.
The build also requires Linux ELF tools, static Linux guest binaries, and Linux host interfaces.

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

`setup` builds the pinned kernel and verifies the guest boundary.
`prereq` lists the exercises and concepts needed directly by the selected exercise.
`read` explains the mechanism and task.
`start` creates or reopens the workspace and prints its editable paths.
`check` builds the module and runs its declared host and guest checks.

Exercise commands accept a serial such as `L19`, a stable ID such as `netfilter-observer`, or an unambiguous prefix.
With no exercise argument, most commands use the current exercise.

```sh
./rootkitlings status
./rootkitlings list
./rootkitlings hint
./rootkitlings solution
```

## Course

| Stage | Exercises | Outcome |
|---|---:|---|
| Module core | L00–L03 | Build and unload a module, return precise initialization errors, own private state, and publish procfs state with complete rollback. |
| Control and execution | L04–L07, M00 | Expose bounded procfs controls and stop timers, workqueues, and event-driven threads without leaving work behind. |
| Identity and observation | L08–L13, M01 | Attribute work to tasks, replace credentials transactionally, authorize one action, and pair probe entry data with return results. |
| Views and detection | L14–L16, M02 | Filter procfs, packed directory records, and bounded read results while preserving an independent evidence view. |
| Network state | L17–L25, M03 | Validate packet bounds, access and mutate socket buffers, own sockets and framed sessions, encode DNS names, and update one owned boot request transactionally. |
| Authenticated operations | L26–L30, C00 | Choose the required protection property, reject replay, add authenticated encryption and signaling, then complete and verify the course rootkit. |

Milestones are cumulative workspaces.
They integrate only mechanisms taught by the preceding lessons.
Run `./rootkitlings list` for the live route and `./rootkitlings prereq EXERCISE` for the direct dependency list.

## Recovery

`restore` and `reset` do different jobs:

```sh
./rootkitlings restore L12
./rootkitlings reset L12
./rootkitlings reset --all --yes
```

- `restore` archives the current edits and recreates that exercise's starting
  workspace. Course progress is unchanged.
- `reset` clears progress from the selected exercise onward and archives the
  affected workspaces. Resetting the entire course requires `--all --yes`.
- `clean` removes declared build outputs but keeps source edits and progress.

## Repository layout

```text
course.toml                         live route, concepts, tags, and tools
curriculum/<serial>-<stable-id>/   one exercise bundle
  README.md                        lesson and task
  hint                             focused hint
  exercise.toml                    metadata, workspace, and checks
  starter/                         optional sparse starting files
  solution/                        reference files or sparse changes
  checks/                          exercise-owned verification
  fixtures/                        optional independent test inputs
rootkitlings                       learner command
engine/                            catalog, workspace, state, and verification
vm/                                pinned guest runtime and shared guest tools
.rootkitlings/                     ignored learner work, state, and archives
```

Learner edits belong under `.rootkitlings/`; files under `curriculum/` define the immutable course bundles.

See [`vm/README.md`](vm/README.md) for the guest runtime.
Run `./rootkitlings help` for all learner commands and `make validate` for repository maintenance checks.
`make compile-db` uses Bear to generate clangd metadata for every physical starter and solution source without changing learner workspaces or progress.
