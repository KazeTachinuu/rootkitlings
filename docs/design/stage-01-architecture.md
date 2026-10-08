# Stage 1 cumulative architecture

Status: accepted architecture decision for the Stage 1 incubation route.
It does not change the public route or authorize implementation outside the incubator.

## Decision summary

Stage 1 grows one module through R02–R06 and then checks it at the first milestone.
The migration map calls that milestone M01; the generated public serial will be M00 because it is the first milestone.
This document uses **M00 (Inspectable core)** below.

The cumulative tree has five files from R02 onward, one component owns each resource, build-time failure injection is deterministic, status publication waits for Stage 2, and allocator tracepoints provide evidence independent of module logs.

## Fixed cumulative tree

R02 creates the complete file set:

```text
Kbuild
include/rootkit.h
core/main.c
core/state.c
status/status.c
```

Every R02, R03, R04, R05, R06, and M00 workspace declares exactly these paths and `build = "."`.
Files that are not yet pedagogically active contain the smallest warning-free private stub; later lessons replace a stub but never add, remove, or rename a declared file.
This constraint makes completion snapshots byte-addressable and compatible without teaching filesystem migration in the middle of the stage.

`Kbuild` always produces `rootkitlings.ko` from the three objects.
The artifact basename and resulting module inventory name `rootkitlings` are stable cumulative ABI constants:

```make
obj-m += rootkitlings.o
rootkitlings-y := core/main.o core/state.o status/status.o
ccflags-y += -I$(src)/include
ccflags-y += -DROOTKITLINGS_FAIL_POINT=$(ROOTKITLINGS_FAIL_POINT)
```

The implementation must provide a default of `0` when `ROOTKITLINGS_FAIL_POINT` is unset.
Concretely, `Kbuild` sets `ROOTKITLINGS_FAIL_POINT ?= 0` before using it in `ccflags-y`.
Checks choose a value on the external-module build command line; learners do not edit source between failure cases.

## Permanent ownership and API

This is a specific module architecture, not a generic resource framework.

### `core/main.c`

`core/main.c` owns only module activation order.
It calls the concrete component entry points in this order:

1. preflight rejection;
2. `rk_state_create()`;
3. `rk_status_create(state)`.

On failure or unload it destroys successfully created components in exact reverse order.
It never allocates, frees, or publishes a component's storage itself.
Failed module initialization performs its unwind locally because `module_exit` is not called after failed init.

### `core/state.c`

`core/state.c` defines the private `struct rk_state`, owns its single allocation, and exports only the Stage 1 functions declared in `include/rootkit.h`:

```c
struct rk_state;

noinline struct rk_state *rk_state_create(void);
noinline void rk_state_destroy(struct rk_state *state);
```

The successful return transfers one live state object to `core/main.c`, which holds the pointer solely to sequence component lifetime.
`rk_state_destroy()` is the only function that frees it.
The caller sets its local pointer to `NULL` immediately after destruction; the API does not promise that destroying `NULL` is useful or repeatable.

### `status/status.c`

`status/status.c` defines the private `struct rk_status`, owns its single allocation, and exports:

```c
struct rk_status;

noinline struct rk_status *rk_status_create(const struct rk_state *state);
noinline void rk_status_destroy(struct rk_status *status);
```

The status object may retain a non-owning state pointer only while the state outlives it.
Consequently status is destroyed before state on every path.
No other file frees status storage, and status never frees or mutates the state object.
All four ownership functions are `noinline` so allocator call sites remain inside their ELF symbol ranges for independent tracing.

### `include/rootkit.h`

The header owns forward declarations, the four concrete lifecycle prototypes, and the failure-point constants.
It exposes neither component layouts nor a callback table, generic resource descriptor, registration framework, linked list, lock, or userspace interface.
Those mechanisms belong to later lessons.

## Build-time failure-injection ABI

Failure injection is a build ABI, not a module parameter or command channel.
That avoids teaching parameter callbacks or input parsing before their lessons.

`ROOTKITLINGS_FAIL_POINT` has these stable integer values throughout Stage 1:

| Value | Name | Injection and required result |
|---:|---|---|
| 0 | `RK_FAIL_NONE` | Normal activation |
| 1 | `RK_FAIL_PREFLIGHT` | `core/main.c` rejects before acquisition with `-ECANCELED` |
| 2 | `RK_FAIL_STATE_ALLOC` | `kzalloc()` returns `NULL`; `rk_state_create()` converts it to `ERR_PTR(-ENOMEM)` and init preserves `PTR_ERR()` |
| 3 | `RK_FAIL_STATUS_CREATE` | `rk_status_create()` returns `ERR_PTR(-EUCLEAN)`; init preserves `PTR_ERR()` and destroys state |

Checks perform a clean external-module rebuild for each value.
They assert the exact loader exit class, module absence in `/proc/modules` and `/sys/module/rootkitlings`, and successful immediate load/unload after rebuilding with value 0.
The check must reject an unknown value at compile time so an accidental spelling cannot silently exercise the normal path.

R04 introduces value 1 and the failed-init rule.
R05 implements the state allocation and value 2; `core/main.c` consumes its pointer-shaped result with `IS_ERR()` and `PTR_ERR()`.
R06 implements the status allocation and value 3, then completes the concrete state-then-status acquisition and status-then-state unwind.
The distinction is deliberate: `kzalloc()` reports raw failure as `NULL`, but the state constructor presents a pointer-shaped-error API and preserves the converted `-ENOMEM`.

## Status publication waits for Stage 2

Stage 1 has an internal status component but no procfs, sysfs, debugfs, character-device, parameter, or other userspace publication.
Its purpose is to establish component ownership and a dependent lifetime without smuggling publication, read-position, permissions, or bounded-read contracts into Stage 1.

R03 proves the component split through composite linking and symbol/static inspection.
R05 proves state lifetime; R06 adds status and proves two-resource reverse unwind through build-time failure injection and allocator tracing.
R07 in Stage 2 is the first lesson allowed to publish status, using procfs with its own ownership and cleanup contract.

The ordinary module artifact in `/sys/module/rootkitlings` is kernel-provided load evidence, not a learner-authored status channel.

## Independent allocation and cleanup evidence

Module log messages are explanatory output only.
They are never accepted as proof that an allocation occurred or was freed.

The pinned guest exposes tracefs `kmem:kmalloc` and `kmem:kfree` events with `call_site=%pS` and `ptr=%p`; `kmalloc` also exposes `bytes_req` and `bytes_alloc`.
Despite the `%pS` declaration, repeated tests on the pinned guest rendered module call sites as raw `0xffffffffc...`
values both while loaded and after unload.
The `ptr` token is opaque and must not be described as a dereferenceable or disclosed kernel address.
Its textual value remains stable across paired allocation and free events and is suitable only for equality correlation within one trace run.

Before boot, the host-side checker reads `rootkitlings.ko`'s ELF symbol table and `.text` section.
It records the `.text`-relative value and size of all four ownership functions and rejects missing, zero-sized, overlapping, or wrong-section symbols.
Distinct ranges enforce the functions' `noinline` contract.
The orchestration layer renders those eight numeric constants into its temporary guest-check copy before `vm/boot` packages it; it does not modify the learner workspace.
These are static expectations from the exact module copied into the guest, not learner log output.

The M00 guest check then uses two trace phases:

1. mount tracefs, verify the two event formats contain `call_site` and `ptr`,
   save their enable state, disable them, and clear the trace;
2. enable `kmalloc`, load the value-0 module, read
   `/sys/module/rootkitlings/sections/.text`, and combine that runtime base with every
   host-derived ELF offset and size;
3. while the module remains loaded, stop tracing and require exactly one
   `kmalloc` raw call-site address inside the calculated `rk_state_create`
   range and one inside the `rk_status_create` range, recording each opaque,
   nonzero `ptr` token;
4. disable `kmalloc`, enable `kfree`, clear the live trace buffer, unload the
   module, disable tracing, and only then read the free-phase trace;
5. for each recorded allocation pointer, require exactly one `kfree` with the
   same pointer and a raw call-site address inside its precomputed
   `rk_state_destroy` or `rk_status_destroy` runtime range;
6. confirm the module is absent, remove checker-owned captures, clear the trace,
   and restore both event enable files to their original state.

The constructor address range assigns each opaque pointer token to the expected owner.
The exact token match proves that the observed allocation was passed to `kfree`; the precomputed runtime function range attributes that free to the corresponding destructor.
A learner cannot satisfy either fact by printing a claimed address.

Tracepoint availability, ELF symbol/section data, the loaded `.text` base, and parseable raw call-site values are pinned-lab prerequisites.
If any prerequisite is absent, or a post-unload call site is not a parseable address in the recorded destructor range, the runtime check fails with an environment-prerequisite or evidence error as appropriate.
It is not reported as a curriculum pass and does not fall back to module logs.
A static or documentation-only validation job may mark the runtime check as not run, but route completion may not.

This evidence is scoped honestly: it proves that the two opaque constructor-attributed pointer tokens observed in that run were later passed to `kfree` from the expected destructor address ranges.
It is not a general leak detector and does not prove absence of every possible kernel leak.
Kmemleak or a diagnostic kernel may be added to maintainer CI later, but neither is required by this Stage 1 learner contract.

## Snapshot lineage and compatibility

The exact lineage is:

```text
R02 immutable template
 `-- R02 passed snapshot
      `-- R03 starting snapshot
           `-- R03 passed snapshot
                `-- R04 starting snapshot
                     `-- R04 passed snapshot
                          `-- R05 starting snapshot
                               `-- R05 passed snapshot
                                    `-- R06 starting snapshot
                                         `-- R06 passed snapshot
                                              `-- M00 starting snapshot
                                                   `-- M00 passed snapshot
```

R02 is the only template-backed kernel workspace in Stage 1.
R03–R06 use completion inheritance from the immediately preceding required checkpoint.
M00 uses the passed R06 snapshot as its starting baseline; the engine's `previous-milestone` rule begins with the next cumulative milestone because no earlier milestone exists for M00.

Compatibility is exact, not inferred:

- all seven checkpoints declare the same five relative files and build root;
- a passed snapshot contains only those declared source files plus its manifest,
  never build output;
- the snapshot manifest records provider stable ID, curriculum revision, file
  modes, and canonical tree hash;
- a consumer records the exact provider snapshot hash when its workspace is
  first materialized;
- rechecking or restoring an earlier provider never rebases an existing
  consumer workspace;
- missing, tampered, differently declared, or revision-incompatible snapshots
  block materialization with a precise error;
- migration may retain a snapshot only through an explicit compatibility rule
  naming both stable IDs and certifying this file/build contract.

R02–R06 remain individually passable because each completion is a warning-free module with inactive future files, not a partial tree that depends on hidden solution content.
M00 adds no source file and no unexplained helper; it asks the learner to complete and test the concrete acquisition/unwind paths already introduced.

## Check boundaries

Stage 1 checks may use external-module build output, ELF/module metadata, loader status, `/proc/modules`, `/sys/module`, tracefs allocator events, and immediate reload as independent evidence.
They may inspect module logs for diagnostics, but a log line never satisfies an ownership, allocation, unwind, or cleanup assertion.

Checks must restore trace event state, unload any successfully loaded module, clear temporary trace data, and power down the disposable guest even after an assertion fails.
No check enables networking, mounts a host directory, writes outside its disposable guest, or targets a host kernel.

## Source basis

This decision follows the official kernel documentation already collected for the curriculum:

- [Building external modules](https://docs.kernel.org/kbuild/modules.html)
- [Driver basics and module lifecycle](https://docs.kernel.org/driver-api/basics.html)
- [Kernel memory allocation](https://docs.kernel.org/core-api/memory-allocation.html)
- [Tracing guide](https://docs.kernel.org/trace/index.html)
