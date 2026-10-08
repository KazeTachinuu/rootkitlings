# Rootkitlings engine v3 requirements

Status: approved design input; implementation in progress.

The engine foundation is usable, but the repository does not yet model the target curriculum.
It currently exposes 65 required checkpoints, no milestones, no electives, and no tracks.

## Requirements and progress

### 1. Generate aliases from canonical course order

Implementation: complete for the current required route.
`Catalog` now generates L/M/C serials from canonical route order and kind; `course.toml` no longer stores an alias table.
Track ordering and elective positions remain pending because no elective exercises are public yet.

Completed:

- Remove `[aliases]`.
- Generate exercise serials by canonical course order and `kind`:
  - lessons: `L00` onward;
  - milestones: `M00` onward;
  - capstones: `C00` onward.

Pending with elective-track adoption:

- Generate track labels separately as `E01` onward.
  `E` identifies a track,
  not an exercise.
- Define canonical all-exercise order as:
  1. `required_route`;
  2. elective track members in track order and member order.
- Reject exercises absent from both the required route and exactly one elective
  track.
- Replace `sequence` with an unambiguous canonical display position, or expose
  `route_position` and `track_position` separately.

### 2. Add authoritative elective tracks

Although the catalog accepts `status = "elective"`, current metadata contains none.
There is no track registry, track membership, ordering, label, or CLI filtering.

Additionally:

- `Catalog.ordered()` returns required items only.
- Catalog export consequently omits electives.
- CLI appends non-required exercises sorted by stage and title, losing
  pedagogical track order.
- CLI treats every non-required status, including `retired`, as an
  elective-like runnable item.

Required course schema:

```toml
[[elective_tracks]]
id = "kernel-debugging"
label = "Kernel debugging"
members = ["qemu-gdb-debugging"]
```

Required validation:

- Track IDs and labels are unique.
- Track member order is stable and duplicate-free.
- Every member exists and has `status = "elective"`.
- Every elective belongs to exactly one track.
- Required and retired exercises cannot belong to tracks.
- Elective prerequisites are reachable without making an elective block the
  required route.
- Retired exercises cannot be started, built, run, or checked.

Required APIs:

- `catalog.required()`
- `catalog.elective_tracks`
- `catalog.track_for(exercise)`
- `catalog.display_order()`
- Export required and elective entries, including track ID, label, and
  position.
- CLI `list` renders the required journey followed by named optional tracks.

The current elective availability rule—direct prerequisites must be completed—is otherwise a reasonable base.

### 3. Derive cumulative milestone ancestry

Current metadata supports arbitrary `baseline = "completion"` plus an explicit parent.
The only instances form an old six-edge L00–L08 chain.
No item has `kind = "milestone"`.

Stage 1 is one continuous construction path: R03 through R05 and M00 use `previous-required` so each checkpoint starts from the exact preceding required pass.
Later milestones and the final capstone use `previous-milestone` so isolated lessons between integrations do not silently become cumulative providers.
Arbitrary earlier completion parents are too permissive and duplicate route structure.

Required schema:

```toml
[workspace]
mode = "cumulative"
baseline = "previous-milestone"
files = ["Kbuild", "src/main.c"]
editable = ["src/main.c"]
build = "."
```

Both derived baselines omit `workspace.parent` and `workspace.source`.
The catalog derives the provider from `required_route`.

An inherited checkpoint may add files explicitly:

```toml
[workspace]
mode = "cumulative"
baseline = "previous-required"
additions = "curriculum/additions/owned-state"
files = ["Kbuild", "include/rootkit.h", "core/main.c", "core/state.c"]
editable = ["Kbuild", "include/rootkit.h", "core/main.c", "core/state.c"]
build = "."
```

`additions` is valid only for an inherited baseline.
Provider mode and build root must match the consumer, provider files must be a subset of consumer files, and the additions directory must contain exactly the newly declared regular files.
Existing paths always come from the verified provider snapshot; additions cannot replace, remove, or rename inherited paths.
The field is required when files are added and forbidden when the file set is unchanged.

Validation must enforce:

- Exactly five required milestones and one required final capstone.
- `previous-required` resolves to the immediate earlier item in `required_route`.
- R03, R04, R05, and M00 use `previous-required`.
- Each cumulative milestone after M00 derives from the preceding milestone.
- The final capstone derives from M04.
- A previous-milestone baseline has an actual earlier milestone.
- Provider and consumer have identical workspace mode and build root.
  Declared files are either identical or follow the explicit additive contract above.
- Electives cannot use or modify the required cumulative lineage.
- An elective may consume a milestone snapshot read-only, but its passed
  snapshot must never become a required milestone baseline.
- Required atomic lessons use immutable templates unless explicitly part of
  cumulative integration.

CLI `baseline()` must ask the catalog for the derived baseline provider rather than reading an exercise-local parent.
Workspace materialization must atomically combine verified provider files with the immutable additions directory and must never source an inherited path from the additions tree.

### 4. Support explicit state migration

Implementation: generic state and migration foundations are complete.
The reviewed 65-to-46 plan, workspace relocation, and CLI orchestration remain pending.

The former exact-prefix rule conflicted with the requirement that a curriculum reorder not change the meaning of saved progress.
Inserting a new checkpoint before completed stable IDs turns valid historical completion into a non-prefix set.

Completed state design:

- Parse saved state before validating it against the active catalog.
- Add explicit, versioned migration definitions; never infer migrations.
- Migration definitions include:
  - source revision;
  - destination revision;
  - stable-ID rename and removal mappings;
  - completion-credit rules for merged checkpoints, normally `all_of`;
  - workspace and snapshot compatibility declarations.
- Perform migration under the state lock.
- Archive the original state before atomic replacement.
- Preserve attempts and disclosure records for unchanged IDs.
- Clear baseline, workspace, and passed hashes unless the migration explicitly
  certifies identical declared files and semantics.
- Never grant a merged checkpoint from only one incomplete source component.
- Validate source and destination state against explicit curriculum contracts.
- Keep completion evidence separate from attempt/disclosure history merging.
- Permit each source history to feed only one destination.
- Require an explicit verifier before retaining compatible snapshot hashes.
- Reject symlinked or non-private state/archive directory boundaries.

For reorder and insertion resilience, completion validation now enforces:

- completed IDs must exist;
- completed exercises must be prerequisite-closed;
- `current` remains the first incomplete required-route ID;
- normal CLI gating still prevents completing future required items.

This permits sparse historical credit after a route insertion without changing stable-ID meaning.

Still pending:

- Encode and review the one-time 65-to-46 migration plan because many IDs are
  merged or replaced.
- Verify compatible snapshots and relocate/archive workspaces before committing
  the state replacement.
- Wire revision mismatch handling to the migration orchestrator with precise
  recovery output.

A revision string change alone is insufficient.

### 5. Separate workspace restoration from progress reset

Implementation: complete for the current engine, with temporary-runtime coverage.

Implemented commands:

- `restore ID`: archive current work and restore the recorded starting
  baseline.
- `clean ID`: remove build outputs only.
- `progress-reset ID`: explicitly remove progress beginning at that checkpoint.
- `progress-reset --all`: explicitly clear course progress.

A required-route progress reset at an ID must:

- Archive affected workspaces.
- Remove completion for that ID and all later required checkpoints.
- Remove dependent elective completions.
- Clear affected workspace, baseline, and passed references from state.
- Leave immutable content-addressed snapshots as unreferenced archives unless
  a separate garbage-collection command is deliberately invoked.
- Preserve earlier attempts and history unless the command explicitly offers a
  stronger scope.

An elective progress reset removes that elective and its elective descendants.

### 6. Remove learner-facing `restart`

Implementation: complete.

Under one curriculum revision, templates are immutable.
For a template-backed workspace, its recorded starting snapshot must equal the pristine template for the lifetime of that revision.
Therefore `reset` and `restart` restore the same content under normal operation.

The removed `restart` command differed only if canonical templates drifted, at which point it silently replaced `baseline_hash`.
Curriculum revision migration must control that situation.

Completed changes:

- Remove the learner-facing `restart` command.
- Rename workspace `reset` to `restore`.
- Handle intentional rebasing only through an explicit curriculum migration or
  rebase operation.
- Remove the unused lower-level `restart_workspace()` primitive.

## Dependency-ordered implementation and tests

### 1. Course registry and catalog ordering

Tests:

- Generated L/M/C serials are deterministic.
- The required route contains exactly 40 lessons, five milestones, and one
  capstone.
- Alias generation is independent of filesystem discovery order.
- Every elective belongs to exactly one of eight tracks.
- Export includes all required and elective exercises.
- Retired entries are excluded from runnable and display routes.

### 2. Milestone baseline semantics

Tests:

- M02 resolves to M01's passed snapshot.
- A missing previous-milestone snapshot blocks materialization.
- An elective pass cannot affect the next required milestone baseline.
- Incompatible cumulative file or build declarations fail catalog validation.
- Additive cumulative declarations require an exact additions tree and reject
  removal, replacement, links, special files, and undeclared extras.
- Atomic materialization copies inherited files from the verified provider
  snapshot and new files from additions, preserving executable-bit class.
- Rechecking an old milestone does not rebase an already-created later
  workspace.

### 3. State migration framework

Tests:

- Same-ID revision migration preserves completion and history.
- Renames preserve mapped history.
- `all_of` merge credit requires every source checkpoint.
- Removed IDs are archived rather than silently retained.
- Incompatible workspace hashes are cleared.
- Compatible hashes are retained only after snapshot verification.
- Migration failure leaves the original state byte-for-byte intact.
- Route insertion preserves prerequisite-closed historical completion.
- An unknown source revision fails with a precise migration error.

### Lineage adoption blockers

Derived baseline selection identifies the expected provider but does not yet prove snapshot provenance.
Snapshot manifests are keyed by the intrinsic tree hash, so adding provider identity or curriculum revision to them would create identity collisions for identical content.
The pending v3 learner-state schema must instead record `baseline_provider` beside `baseline_hash`, and restore/materialization must validate that provider ID and the state's curriculum revision while the snapshot manifest remains content-intrinsic.

The learner build path also currently assumes the module artifact is `hello.ko`, while the target cumulative module is `rootkitlings.ko`.
The course registry should define one authoritative default module artifact, with an exercise-level override only where needed, rather than duplicating the same output name across every exercise.

### 4. Track-aware CLI behavior

Tests:

- The required route never waits for an elective.
- A locked elective reports its missing prerequisites.
- Elective chains unlock correctly.
- Retired work is not runnable.
- `list` groups exercises by required route and elective track.
- Stable IDs and generated serials resolve identically.

### 5. Restore and progress reset

Tests:

- `restore` archives edits and restores the recorded baseline without changing
  completion.
- Resetting required progress truncates downstream required credit.
- Dependent elective credit is removed.
- Earlier progress remains.
- `--all` clears state only after explicit confirmation and scope.
- An interrupted progress reset leaves state and work recoverable.

### 6. Remove learner-facing restart

Tests:

- No command, help output, or learner documentation exposes `restart`.
- Template drift within one revision is detected as catalog or repository
  inconsistency rather than silently rebasing.
- Revision migration is the only operation allowed to establish a new template
  baseline.
