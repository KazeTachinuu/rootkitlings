# Course design

Rootkitlings teaches Linux kernel development by building a rootkit one
observable mechanism at a time.
The required route contains 31 lessons, four integration milestones, and one
capstone.

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

Each lesson teaches one reusable mechanism through success, rejection, and
cleanup evidence.
Milestones integrate only mechanisms already practiced.

The exact graph is metadata, not prose:

- `course.toml` owns the route and controlled vocabularies.
- Each `exercise.toml` owns direct prerequisites, concepts, workspace
  composition, and verification cases.
- L, M, and C serials are display names; stable IDs are durable state keys.

## Prerequisites

Entry knowledge is attached where first used, not copied into every lesson.
`./rootkitlings prereq ID` prints direct exercise dependencies and the specific
outside knowledge used by that exercise.

Prerequisites contain only direct edges; validation rejects transitive
duplicates.
Every required concept must be entry knowledge or taught by a prerequisite.

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

The engine composes inherited solutions, starter files, and sparse changes.
It rejects undeclared, missing, empty, or redundant content so each exercise
keeps a small, complete delta.

## Verification

Checks prove behavior rather than requiring one source spelling.
Depending on the mechanism, evidence includes:

- warning-free external-module builds against the pinned kernel;
- host-side exhaustive or boundary-focused corpora;
- semantic mutants that compile but implement the wrong behavior;
- fresh QEMU guests for success, precise failure, rollback, and cleanup;
- independent views instead of trusting module-owned status alone;
- repeated load/unload and cancellation paths for resource ownership.

Text inspection is reserved for static contracts with no runtime observation.

## Runtime boundary

The course pins Linux 7.2.9 in a disposable x86-64 QEMU guest.
The host kernel and headers are outside the build contract.
KVM is optional.
The guest has no persistent disk, host mount, or network unless a check requests
that capability.

Kernel-version-sensitive API choices and primary references are documented in
the README for the lesson that uses them.
