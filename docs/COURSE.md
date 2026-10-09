# Course design

Rootkitlings teaches Linux kernel development by building a rootkit one observable mechanism at a time.
The required route contains 31 lessons, four integration milestones, and one capstone.

## Dependency spine

```text
module lifecycle
`-- owned state and procfs publication
    |-- bounded control and resident execution
    |   `-- M00 controlled resident core
    |       `-- identity, credentials, and syscall telemetry
    |           `-- M01 authorized actions and telemetry
    |               `-- bounded view filters and independent evidence
    |                   `-- M02 concealment evidence map
    `-- packet boundaries, sockets, framing, and guarded file updates
        `-- M03 remote resident agent
            `-- authenticated messages, encryption, and DNS-shaped signals
                `-- C00 authenticated resident agent
```

The graph is not a list of every API used by a rootkit.
Each lesson introduces one reusable mechanism with an observable success path, a rejection or failure path, and cleanup evidence.
Milestones integrate mechanisms already practiced.

The exact graph is metadata, not prose:

- `course.toml` defines the route, controlled vocabularies, entry knowledge,
  tools, stages, and tradecraft tags.
- Each `curriculum/required/<stable-id>/exercise.toml` defines direct exercise
  prerequisites, concepts taught, knowledge used, workspace composition, and
  verification cases.
- L, M, and C serials are generated presentation labels.
  Stable IDs own state
  and filesystem paths.

## Prerequisites

Entry knowledge is attached only where first used.
It is not copied into every lesson.
`./rootkitlings prereq ID` prints direct exercise dependencies and the specific outside knowledge used by that exercise.

Exercise prerequisites contain only direct edges.
Transitive duplicates are rejected by catalog validation.
A required concept must be either declared entry knowledge or taught by a prerequisite.

## Workspace composition

A learner edits an isolated tree under `.rootkitlings/work/`.
Bundles contain:

```text
exercise.toml
README.md
hint
starter/      optional files applied to the inherited tree
solution/     sparse reference changes
checks/       host, guest, failure, and maintainer checks
fixtures/     optional independent test inputs
```

The engine recursively composes inherited solutions, starter files, and sparse solution changes.
It rejects undeclared files, empty directories, missing new files, and byte-identical inherited copies.
This keeps every mechanism's delta small without weakening the complete build.

## Verification

Checks prove behavior rather than requiring one source spelling.
Depending on the mechanism, evidence includes:

- warning-free external-module builds against the pinned kernel;
- host-side exhaustive or boundary-focused corpora;
- semantic mutants that compile but implement the wrong behavior;
- fresh QEMU guests for success, precise failure, rollback, and cleanup;
- independent views instead of trusting module-owned status alone;
- repeated load/unload and cancellation paths for resource ownership.

Text inspection is used only for static contracts that have no useful runtime observation, such as exported metadata or a required build-time diagnostic.

## Runtime boundary

The course pins Linux 7.2.9 and boots it in a disposable x86-64 QEMU guest.
The host kernel and host headers are not part of the build contract.
KVM is an optional accelerator.
The default guest has no persistent disk, host mount, or network device; a verification case requests only the capability it needs.

Kernel-version-sensitive API choices and primary references are documented in the README for the lesson that uses them.
