# Rootkitlings engine v3 requirements

Status: approved design input; implementation in progress.

The engine foundation is usable, but the repository does not yet model the target curriculum.
It currently exposes 65 required checkpoints, no milestones, no electives, and no tracks.
The target required route has 32 lessons, four milestones, and one capstone: 37 scored checkpoints.
A mandatory authorization and containment setup gate sits before that route and carries no serial or completion credit.

## Requirements and progress

### 0. Require the unscored setup gate

Implementation: complete.

The setup gate is outside the exercise catalog and required route.
It grants no completion, has no learner solution, and must pass before commands that build or execute course work.
It records only a local acknowledgement tied to the relevant course revision and VM boundary settings.

Acceptance requires explicit authorization, a disposable pinned guest, no persistent disk, production data, host mount, or network, at least two stop conditions, preflight rejection of weaker settings, a guest boundary probe, bounded shutdown, and removal of temporary guest state.
Changing a relevant boundary setting invalidates the acknowledgement.
Status and list output may report the gate separately but must not count it among the 37 scored checkpoints.
Networking exercises may later enable only capabilities declared by their metadata; the gate verifies the default module-free guest, not a course-wide ban.

`setup` shows the boundary and current state without recording acceptance.
`setup --accept` runs the host diagnostic and module-free containment probe before writing a private local stamp tied to the policy, course revision, host kernel, and exact VM boundary inputs.
`start`, `build`, `check`, `run`, and `gdb` require a current stamp; reading and recovery commands remain available.

### 1. Generate aliases from canonical course order

Implementation: complete in the catalog engine.
`Catalog` now generates L/M/C serials from canonical route order and kind; `course.toml` no longer stores an alias table.
When elective tracks are declared, the catalog generates their E-series labels and the canonical display order without storing either in metadata.

Completed:

- Remove `[aliases]`.
- Generate exercise serials by canonical course order and `kind`:
  - lessons: `L00` onward;
  - milestones: `M00` onward;
  - capstones: `C00` onward.

- Generate track labels separately as `E01` onward.
  `E` identifies a track,
  not an exercise.
- Define canonical all-exercise order as:
  1. `required_route`;
  2. elective track members in track order and member order.
- Reject required or elective exercises that are absent from their declared
  route or track.
- Replace `sequence` with an unambiguous canonical display position, or expose
  `route_position` and `track_position` separately.

### 2. Add authoritative elective tracks

Implementation: registry, generated labels, membership validation, API, export, and learner list grouping are complete.
The live course metadata has no elective exercises yet; the eight target tracks and their lesson assets remain to be added during route adoption.

Required course schema:

```toml
[[elective_tracks]]
id = "kernel-debugging-track"
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

Still pending with target metadata adoption:

- Populate all eight reviewed tracks and their member exercises.
- Validate that elective dependency edges follow canonical reachable order.
- Reject execution commands for any retained `status = "retired"` metadata.

### 3. Derive cumulative milestone ancestry

Current metadata supports arbitrary `baseline = "completion"` plus an explicit parent.
The only instances form an old six-edge L00–L08 chain.
No item has `kind = "milestone"`.

Stage 1 is one continuous construction path: R02 through R04 use `previous-required` so each checkpoint starts from the exact preceding required pass.
R04 is the cumulative project root.
The first milestone derives from that declared root; later milestones and the final capstone use `previous-milestone` so isolated lessons between integrations do not silently become cumulative providers.
Arbitrary earlier completion parents are too permissive and duplicate route structure.

For M00, L05-L07 are knowledge prerequisites, not workspace providers.
The learner starts with their own passed R04 tree and applies the separately practiced control, timer, and thread mechanisms to it.
The resulting design unifies timer and proc producers behind one capacity-one atomic request path consumed by the resident thread; it does not retain L06's practice workqueue.
Capacity one is an intentional integration refactor of L07's counted, non-coalescing admission because periodic demand must not accumulate a backlog.
This is ordinary `cumulative-root` materialization, not an automatic merge of lesson workspaces or a new engine feature.

Required course schema:

```toml
cumulative_root = "proc-publication"
```

The value is a stable exercise ID, not a serial alias.
It declares the one lesson snapshot from which the first cumulative milestone grows.

Required workspace schemas:

```toml
[workspace]
mode = "cumulative"
baseline = "cumulative-root"
files = ["Kbuild", "src/main.c"]
editable = ["src/main.c"]
build = "."
```

The first milestone alone uses `cumulative-root`.
Later milestones use:

```toml
[workspace]
mode = "cumulative"
baseline = "previous-milestone"
files = ["Kbuild", "src/main.c"]
editable = ["src/main.c"]
build = "."
```

All derived baselines omit `workspace.parent` and `workspace.source`.
The catalog derives the provider from `required_route` and the course-level cumulative root.

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

- Exactly four required milestones and one required final capstone.
- `cumulative_root` names an earlier required cumulative lesson and resolves to
  its passed snapshot.
- Only the first required milestone may use `cumulative-root`, and it must use it.
- `previous-required` resolves to the immediate earlier item in `required_route`.
- R02, R03, and R04 use `previous-required`.
- Each cumulative milestone after M00 derives from the preceding milestone.
- The final capstone derives from M03.
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

#### Reference solution overlays

Implementation: complete in the engine and maintainer tooling.

`Catalog.solution_tree()` recursively composes a derived exercise's sparse solution overlay over its baseline provider and validates the result against the declared workspace files.
`Catalog.materialize_solution()` writes that composed tree atomically for builds and runtime checks.
Repository validation and solution tests consume these APIs instead of copying raw solution directories, so sparse overlays are tested as the learner's complete eventual source tree.
The learner-facing `solution` command prints complete recursive trees for template references and a deterministic unified diff for each derived reference, naming its exact provider and omitting unchanged inherited files.
Disclosure state is recorded only after composition and rendering succeed.

Still pending:

- Adopt sparse overlays in target-route metadata and delete the temporary
  compatibility branch for legacy explicit-parent full solutions.

### 4. Support explicit state migration

Implementation: generic state and migration foundations are complete.
The reviewed 65-to-37 mapping, workspace relocation, and route-specific migration orchestration remain pending.

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

- Encode and review the one-time 65-to-37 migration plan because many IDs are
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

### Exact prerequisite output

Implementation: selective assumption rendering and redundant-edge validation are complete; final target metadata still needs an asset-by-asset knowledge audit.

`prerequisites` contains direct exercise gates only.
The catalog rejects duplicate edges and an edge already implied through another direct prerequisite.

`requires` contains every concept or general competency the learner applies in that checkpoint but does not learn there.
An entry in the course-level `assumptions` registry is not displayed globally; its competency text appears only when the exercise names that concept in `requires`.
The CLI never dumps transitive concept closure by default.

This keeps `prereq` exhaustive for the selected task without repeating the whole course or attaching networking knowledge to L00.
Broad assumption bundles must be split when final assets need only part of the bundle; metadata must not use prose placeholders such as `baseline assumptions` or `baseline networking`.

## Dependency-ordered implementation and tests

### 1. Course registry and catalog ordering

Tests:

- Generated L/M/C serials are deterministic.
- The required route contains exactly 32 lessons, four milestones, and one
  capstone.
- Alias generation is independent of filesystem discovery order.
- Every elective belongs to exactly one of eight tracks.
- Export includes all required and elective exercises.
- Retired entries are excluded from runnable and display routes.

### 2. Milestone baseline semantics

Tests:

- M01 resolves to M00's passed snapshot.
- M00 resolves to the course-level cumulative root's passed snapshot.
- M00 materialization contains the passed R04 tree and declared M00 additions,
  never files copied from L05-L07 workspaces.
- A missing cumulative-root snapshot blocks the first milestone.
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

### Lineage provenance

Implementation: complete in learner-state schema version 3.

Snapshot manifests are keyed by the intrinsic tree hash, so adding provider identity or curriculum revision to them would create identity collisions for identical content.
Learner state records `baseline_provider` beside `baseline_hash`, while snapshot manifests remain content-intrinsic.
Template baselines store an empty provider; inherited baselines store the exact catalog-derived stable ID.
State validation rejects missing, unknown, mismatched, or incomplete providers.
The explicit schema-v2 migration archives the original bytes, derives provenance once from the authoritative catalog, writes schema version 3 atomically, and never falls back to inference when loading version-3 state.

The catalog defines `default_module_artifact = "hello.ko"` once in `course.toml`.
A template workspace may set a sparse `module_artifact` override, such as `rootkitlings.ko` for the cumulative module.
Inherited workspaces derive the provider's effective artifact and reject conflicting overrides.
Artifact values are portable `.ko` basenames; paths, traversal, whitespace, and option-like names are invalid.
`Catalog.module_artifact(exercise)` is the sole resolver exposed to build and verification consumers.

Verification build variants are declarative `[[verification.cases]]` records.
Each case has a stable `id`, learner-facing `label`, string-to-string `build_variables`, an `expect_build` result, an optional guest `check`, and an optional `build_output_contains` assertion.
Successful builds require an existing guest check and cannot assert build output.
Expected build failures have no guest check and require a bounded printable output assertion.
Case IDs and case-insensitive labels are unique per exercise, make-variable names are restricted to portable identifier syntax, and all scalar values are bounded printable strings.
Manifests without cases resolve through `Catalog.verification_cases()` to one compatible default successful case using the exercise's existing guest check; exported catalog data always contains the resolved cases.

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
