# Course design

Rklings teaches Linux kernel development by building a rootkit one observable
mechanism at a time.
The required route contains 26 lessons, nine milestones, and one final project.

## Dependency spine

```text
L00 -> L01 -> L02 -> L03 -> L04 -> L05
L05 -> L06
L05 -> L07
L06 + L07 -> M00 resident core

L04 -> L08 -> L09
L05 + L09 -> L10 -> M01 one-use operator key
L05 -> L11 -> L12
L11 -> L13
M00 + M01 + L12 -> M02 activity recorder
L12 + L13 -> L14
L12 + L13 -> L15
M02 + L14 -> M03 concealment evidence map

L16 -> L17
L07 -> L18
L17 + L18 -> L19 -> M04 packet redirector

L03 -> L20
L07 + L20 -> M05 stoppable call-home session
L06 + L09 + L20 -> L21 bounded root-shell bridge
L03 -> M06 guarded file transaction
M03 + M05 + M06 -> M07 call-home agent and reversible load request

L02 + L22 -> L23
M01 + L23 -> L24 replay-safe authenticated message
L23 -> L25 authenticated encryption
M07 + L21 + L25 -> M08 root reverse shell with authenticated trigger

L15 + M04 + M08 -> C00 complete rootkit
```

`+` means that every listed exercise is a direct prerequisite.

Each lesson isolates a reusable mechanism.
Each milestone turns nearby mechanisms into a visible result.
Both prove success, rejection, and cleanup behavior.

The exact graph is metadata, not prose:

- `course.toml` owns the route and controlled vocabularies.
- Each `exercise.toml` owns direct prerequisites, concepts, workspace composition, and verification cases.
- L, M, and C serials are display names; stable IDs are durable state keys.

## Prerequisites

Entry knowledge is attached where first used, not copied into every lesson.
`./rklings prereq ID` prints direct exercise dependencies and the specific
outside knowledge used by that exercise.

Prerequisites contain only direct edges; validation rejects transitive
duplicates.
Every required concept must be entry knowledge or taught by a prerequisite.

## Workspace composition

A learner edits an isolated tree under `work/<serial>/`.
Build outputs, internal state, snapshots, and backups stay under `.rklings/`.
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

M07 writes and rolls back a guarded module-load request.
It does not reboot the guest or claim that the module survives a reboot.

Kernel-version-sensitive API choices and primary references are documented in
the README for the lesson that uses them.
