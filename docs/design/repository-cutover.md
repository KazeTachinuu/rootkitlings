# Repository cutover

Status: accepted cleanup target; execute only with the completed v3 route and state migration.

## Why the repository is temporarily large

The repository currently carries two course layouts.

The public v2 catalog still depends on:

- `lings/`
- `capstones/`
- `curriculum/templates/`
- `curriculum/support/`
- `curriculum/solutions/`

The replacement route is being built under `curriculum/incubator/`.
These are different implementations, not byte-for-byte duplicate files.
Removing the public trees before route adoption would break the learner CLI and saved progress.

## Final root

```text
.
|-- README.md
|-- Makefile
|-- course.toml
|-- rootkitlings
|-- curriculum/
|   |-- required/
|   `-- electives/
|-- engine/
|-- scripts/
|-- tests/
|-- vm/
|   |-- README.md
|   |-- boot
|   |-- init
|   |-- checks/
|   `-- tools/
`-- docs/
```

The formatting and editor configuration files remain at the root.

Each stable exercise belongs in one bundle:

```text
curriculum/required/module-build-lifecycle/
|-- exercise.toml
|-- README.md
|-- hint
|-- template/
|-- solution/
`-- checks/
```

Lessons, milestones and the capstone share `curriculum/required/`.
Their metadata kind supplies the distinction; separate root-level content trees do not.
Generated L, M and C serials remain route labels rather than filesystem identities.

## Namespace decisions

- Keep `rootkitlings`, `course.toml`, `curriculum`, `vm`, `scripts`, `tests` and `docs`.
- Move the Python course engine from the generic `lib/` name to `engine/`.
- Move the guest assertion library from `lib/check.sh` to `vm/checks/lib.sh`.
- Move guest verification programs from the generic `tools/` name to `vm/tools/`.
- Remove the temporary `rNN-` and `mNN-` directory prefixes when bundles move to their stable IDs.
- Remove implementation-status documents after their durable decisions exist in metadata, tests and concise architecture documentation.

`module` is a Make target, not an extra root directory.
Renaming it does not improve the learner model and is not part of this cleanup.

## Generated and learner-owned data

The following stay ignored:

- `.rootkitlings/` learner state, archives, snapshots and workspaces
- built guest verification binaries
- Python caches
- kernel module build products
- compile databases and maintainer caches

Never use `git clean -fdX` for repository maintenance because it would delete `.rootkitlings/`.
Learner progress is changed only through the explicit Rootkitlings commands.
A maintenance cleanup target may remove named build products and caches but must never include `.rootkitlings/`.

Copyrighted or supplied references are intentionally trackable.
There is no ignore rule for `given_files/`, `given_files_tar.bz2` or `epirootkit-slides.pdf`.
If several such files are added, place them in one tracked `references/` directory instead of scattering them across the root.

## Atomic migration order

1. Finish and validate every v3 required and elective bundle while v2 remains public.
2. Freeze stable IDs, the route and the per-exercise bundle schema.
3. Implement and test the explicit 65-to-38 learner-state and workspace migration.
4. Move incubator bundles to stable-ID paths under `curriculum/required/` and `curriculum/electives/`.
5. Adopt the v3 `course.toml`, bundled-asset validation, README, links and compile-database discovery together.
6. Validate the new catalog, every reference solution, migration from representative old states, a clean checkout and the learner CLI.
7. Remove the five legacy content trees explicitly with `git rm`.
8. In a separate mechanical commit, move `lib/*.py`, `lib/check.sh` and `tools/` to their final namespaces and update every consumer.
9. Consolidate durable research and architecture decisions, then delete temporary implementation and migration documents.
10. Search for every stale path and run both quick and full validation.

The final removal must be explicit and reviewable.
It must not use a broad cleanup command or infer that an untracked path is disposable.

## Cutover proof

The final check must include:

```sh
rg -n 'lings/|capstones/|curriculum/(templates|support|solutions|incubator)|lib/check\.sh|tools/' \
  README.md Makefile course.toml rootkitlings curriculum docs engine scripts tests vm
git status --short
make validate-quick
make validate
```

Any intentional remaining match must name its compatibility purpose and removal condition.
