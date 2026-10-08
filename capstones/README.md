# Capstones

Capstones combine mechanisms already introduced by the lings.
They are integration exercises: define ownership, acquire resources in order, unwind failures in reverse order, synchronize shared state, and leave no live hook or worker behind.

Each capstone records its taught concept, directly applied concepts, minimal prerequisites, and sequence in `exercise.toml`.
The CLI interleaves all 11 capstones into the 54-ling progression; from the repository root, use `./rootkitlings list` for the canonical route instead of choosing one by directory order.

## Design

Every source file should own one resource or mechanism:

```text
                         hello.ko
                            ^
                            | Kbuild links
                +-----------+-----------+
                |                       |
             main.o                 feature.o
                ^                       ^
                |                       |
     acquisition and rollback      one mechanism
                \                       /
                 +----------+----------+
                            |
                    shared contracts
```

`main.c` owns acquisition order and rollback.
Mutable state remains private to the component that protects it, and the shared header contains only contracts used across translation units.

Split files when two components own different state, resources, or failure paths—not by line count.
Avoid a generic `common.c` that obscures those boundaries.

## Work

```sh
./rootkitlings read C00
$EDITOR capstones/0-config-store/store.c
./rootkitlings check C00
```

The lesson README names every file and function in scope.
`check` builds the complete directory and exercises observable behavior in a fresh VM.
Use `hint` for the ownership or failure-ordering invariant; use `solution` only when you want to compare every reference file with the scaffold.

The final capstones increasingly emphasize independent defensive views: a technique is not complete merely because one reporting path no longer shows its underlying object.

Return to the [course overview](../README.md) or review the [ling progression](../lings/README.md).
