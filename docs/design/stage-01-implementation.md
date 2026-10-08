# Stage 1 implementation slice

Status: accepted Stage 1 design; implementation and route migration remain pending.
Nothing here changes the public course route yet.

## Outcome

A mandatory setup gate establishes authorization and containment before the scored course begins.
Stage 1 then grows one inspectable multi-file module through four mechanisms and one milestone.
Each checkpoint changes learner-owned source, demonstrates an observable result, and proves rejection or cleanup as well as success.

| Unit | Learner-owned change | Observable success | Negative and cleanup evidence | Reused stable IDs |
|---|---|---|---|---|
| Setup gate (unscored) | Confirm authorization, scope, stop conditions, and disposable-lab settings | Pinned diskless guest boots inside the declared boundary | Unsafe settings stop before boot; guest shutdown removes temporary state | New setup flow, not an exercise |
| R01 Module build and lifecycle (public L00) | Build `rootkitlings.ko`; add metadata and init/exit callbacks | Metadata, symbols, `/sys/module`, and module inventory agree | Unload removes both inventory views; immediate reload succeeds | `module-lifecycle`, `module-build` |
| R02 Precise rejection | Add one controlled init rejection that preserves its negative errno | Loader reports the selected error and the module remains absent | Both inventory views stay absent; rebuilding without injection permits a clean load | `init-failure` |
| R03 Owned kernel state | Add the real header and state component; allocate and release private state | Composite module links and state ownership is statically inspectable | Forced allocation failure preserves `-ENOMEM`, publishes nothing, and permits retry | `multi-file-module`, `kernel-allocation`, `error-pointer` |
| R04 Procfs entry and rollback | Publish a real procfs entry after state acquisition | Proc path, mode, module inventory, and state allocation agree | Forced publication failure releases state; unload removes the proc entry; immediate retry succeeds | `initialization-rollback`, `proc-entry` |
| M00 Inspectable core | Integrate and explain the complete acquisition and unwind paths | The cumulative module and proc status load repeatedly and every owner is inspectable | Every failure point leaves no module, proc entry, or owned state behind | First generated milestone |

## Setup-gate acceptance contract

The setup gate is mandatory but unscored.
It must pass before `start`, `build`, `check`, `run`, or `gdb` can execute course work.
It does not appear in the required route and grants no completion credit.

Acceptance requires:

- an explicit statement that the learner is authorized to use the local lab;
- a disposable pinned guest with no persistent disk, production data, host mount, or network;
- at least two clear stop conditions and an understood cleanup action;
- a preflight rejection before QEMU starts when any boundary is weakened;
- a guest probe that confirms the expected kernel interfaces and the absence of forbidden mounts and networking;
- shutdown within the timeout and removal of temporary guest state.

Later networking exercises may enable only their declared guest capability.
The setup gate verifies the safer default; it does not claim that every course check is offline.

The gate stores only the minimum local acknowledgement needed to avoid repeating a successful preflight.
It has no reference solution because authorization cannot be copied from an example.
Changing the relevant VM or scope settings invalidates the acknowledgement and requires the gate again.

## Cumulative source contract

The accepted [Stage 1 cumulative architecture](stage-01-architecture.md) defines file ownership, failure injection, allocator evidence, and snapshot compatibility.

R01 begins with only:

```text
Kbuild
core/main.c
```

R02 inherits those paths and introduces precise build-time failure selection.
R03 additively introduces `include/rootkit.h` and `core/state.c` when the header and state owner become real.
R04 additively introduces `interface/proc.c` when the procfs resource and reverse rollback become real.
M00 inherits the R04 completion without adding a file or hidden implementation.

No checkpoint ships an empty future translation unit or an unused ownership API.
The public module artifact is `rootkitlings.ko` from R01 onward.
Stage 1 publishes the procfs resource; the bounded read contract remains the next mechanism.

The current stable-ID exercises remain source material until migration.
Migration must map completion and compatible snapshots explicitly; it must not reconstruct a baseline from standalone exercise directories.

## M00 readiness

M00 is ready to author when the cumulative R01–R04 checks and overlays exist together:

- `core/state.c` owns heap state and `interface/proc.c` owns one real proc entry;
- `core/main.c` owns activation and reverse teardown order but not either resource;
- R02 introduces preflight rejection, R03 adds state allocation failure, and R04 adds an explicitly synthetic publication-boundary failure plus reverse unwind;
- tracefs correlates the opaque state-allocation token with its destructor free while procfs and module inventory independently expose publication and cleanup;
- every workspace inherits the immediately preceding required snapshot;
- sparse reference overlays compose to the same complete tree used by checks.

A standalone M00 scaffold would bypass the learning and snapshot lineage and is prohibited.

## Acceptance tests before route adoption

- The setup gate rejects missing authorization, unsafe scope, default networking, persistence, host mounts, or production data before boot.
- Its guest probe exits within the timeout and leaves no temporary guest state.
- R01–R04 each have a positive control, a negative control, and independent cleanup evidence.
- Additive transitions reject missing, extra, linked, or special files.
- M00 injects every declared failure point separately and reloads immediately after each failure.
- The complete Stage 1 workspace builds with `W=1` and no warning.
- The public route remains unchanged until state migration and cumulative snapshot tests pass.
