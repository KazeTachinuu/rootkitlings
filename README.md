# Rootkitlings

Learn Linux kernel engineering and rootkit tradecraft by building small, observable modules.

A **ling** is a focused lesson; a **capstone** integrates mechanisms already learned.
The catalog currently contains 54 lings and 11 capstones in one metadata-defined route.

All modules run against Linux 7.2.x in a disposable QEMU guest.
Do not load them on the host.

The course assumes working C, Unix shell, Linux process/file, and basic networking knowledge.
Run `./rootkitlings prereq L00` for the canonical course-entry list.

## Start

```sh
make doctor
./rootkitlings setup
./rootkitlings read L00
./rootkitlings start L00
$EDITOR .rootkitlings/work/module-lifecycle/hello.c
./rootkitlings check L00
```

`make doctor` reports missing kernel artifacts, tools, and module dependencies.
`setup` prepares the pinned kernel and verifies the disposable guest boundary.
KVM is optional.

## Work

```text
read the contract -> edit the printed path -> check in a clean VM
        ^                                      |
        +---------------- fix -----------------+
```

`read` gives the context, expected behavior, and exact task.
`start` creates an isolated workspace and prints its path.
`check` builds with warnings enabled, boots a fresh guest, exercises live behavior, and verifies cleanup.

If you get stuck, `hint` gives a conceptual nudge and `solution` shows the reference implementation.
`restore` archives edits and restores an exercise's starting files without changing progress.
`reset` is the separate, explicit command for resetting course progress; resetting everything requires `reset --all --yes`.
Run `./rootkitlings help` for the complete command list and `./rootkitlings list` for the metadata-backed route.

## Course

| Stage | Focus |
|-------|-------|
| [Foundation](lings/00_foundation/README.md) | Module lifecycle, builds, memory, and rollback |
| [Procfs](lings/01_procfs/README.md) | Entry lifetime and bounded text interfaces |
| [Interfaces](lings/02_interfaces/README.md) | Errors, lists, devices, ioctl, and userspace helpers |
| [Concurrency](lings/03_concurrency/README.md) | Timers, workers, threads, waits, and shared state |
| [Introspection](lings/04_introspection/README.md) | Tasks, credentials, symbols, and randomness |
| [Instrumentation](lings/05_instrumentation/README.md) | Probes, arguments, and hook lifecycle |
| [Visibility](lings/06_visibility/README.md) | Altered views and independent evidence |
| [Networking](lings/07_networking/README.md) | Packet paths, sockets, channels, and DNS framing |
| [File I/O](lings/08_file_io/README.md) | Bounded durable updates and rollback |
| [Cryptography](lings/09_crypto/README.md) | Obfuscation, encryption, authentication, and rendezvous |

[Capstones](capstones/README.md) appear in the route after their prerequisite mechanisms.
They combine ownership, failure handling, concurrency, and defensive observation across several source files.

## Workspace

```text
course.toml              route and controlled vocabularies
lings/                   focused lesson contracts and metadata
capstones/               integration contracts and metadata
curriculum/templates/    immutable starting code
curriculum/support/      checks and progressive hints
curriculum/solutions/    reference implementations
.rootkitlings/work/      learner-owned working copies
lib/ and scripts/        course engine and maintenance commands
tools/ and vm/           guest helpers and disposable runtime
```

The CLI derives paths, capabilities, tools, and checks from metadata; exercise files do not duplicate them.
Learner state and workspaces stay under the ignored `.rootkitlings/` directory.
Guest filesystems are discarded on exit, and networking remains disabled unless an exercise requests it.

## More

- [Ling roadmap and engineering rules](lings/README.md)
- [Capstone design and source layout](capstones/README.md)
- [VM runtime details](vm/README.md)
- [Linux kernel documentation](https://docs.kernel.org/)

Run `make compile-db` to generate clangd compile commands.
Run `make validate` to validate course structure, documentation, scripts, and reference builds.
