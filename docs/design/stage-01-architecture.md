# Stage 1 cumulative architecture

Status: accepted architecture decision for the Stage 1 incubation route.
It does not change the public route.

## Decision

Before Stage 1, every learner passes a mandatory, unscored setup gate.
The gate confirms authorization, the disposable guest boundary, networking disabled by default, no host mounts, stop conditions, and cleanup after shutdown.
It creates no exercise completion, serial, score, or reusable solution and is not R01.

Stage 1 grows one `rootkitlings.ko` module without predeclared placeholder files or temporary APIs.
Each file appears only when its mechanism becomes active:

| Checkpoint | Declared workspace paths | Mechanism introduced |
|---|---|---|
| R01 Module build and lifecycle (public L00) | `Kbuild`, `core/main.c` | External build, metadata, load, unload |
| R02 Precise failed init | unchanged | Build-time failure input and exact preflight rejection |
| R03 Owned state | add `include/rootkit.h`, `core/state.c` | Composite linking, opaque ownership boundary, heap lifetime, error-pointer contract |
| R04 Reverse rollback | add `status/status.c` | Second owned resource and reverse partial-init unwind |
| M00 Inspectable core | unchanged from R04 | Integrated failure, cleanup, reload, and allocator evidence |

The first milestone is **M00 (Inspectable core)** in both design and generated learner-facing serials.

There is no separate “owned components” checkpoint.
Composite Kbuild and private headers are supporting techniques taught when R03 uses them for real state ownership.

## Stable module identity

The artifact basename and module inventory name are stable Stage 1 ABI constants:

```text
rootkitlings.ko
/sys/module/rootkitlings
rootkitlings in /proc/modules
```

R01 builds one object:

```make
obj-m += rootkitlings.o
rootkitlings-y := core/main.o
```

R03 changes the existing Kbuild when the state component becomes real:

```make
obj-m += rootkitlings.o
rootkitlings-y := core/main.o core/state.o
ccflags-y += -I$(src)/include
```

R04 appends the real status object:

```make
rootkitlings-y := core/main.o core/state.o status/status.o
```

No earlier lesson ships an empty state/status translation unit, declares an unused ownership API, or asks the learner to implement a function that is replaced before it is consumed.

## R01: module build and lifecycle

R01 owns only `Kbuild` and `core/main.c`.
`core/main.c` registers successful init and exit callbacks and declares the GPL license and `Rootkitlings module lifecycle` description.

Independent evidence is the warning-free external build, static metadata and lifecycle symbols, successful insertion, agreement between `/proc/modules` and `/sys/module/rootkitlings`, disappearance after removal, and immediate reload.
Module logs may aid diagnosis but never prove presence or cleanup.

## R02: precise failed initialization

R02 keeps the two R01 paths and introduces deterministic build-time failure injection.
Kbuild supplies an overridable default:

```make
ROOTKITLINGS_FAIL_POINT ?= 0
ccflags-y += -DROOTKITLINGS_FAIL_POINT=$(ROOTKITLINGS_FAIL_POINT)
```

`core/main.c` defines only the values needed so far:

```c
enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_PREFLIGHT = 1,
};
```

Unknown values fail compilation.
Value 1 makes init return `-ECANCELED` before acquiring any resource.
Value 0 preserves the R01 successful lifecycle.

The check performs isolated external-module builds for both values.
For value 1, a guest helper calls `finit_module()` and requires the numeric `errno` value `ECANCELED`; localized `insmod` text is not evidence of the error class.
The check requires absence before and after both rejected attempts in `/proc/modules` and `/sys/module/rootkitlings`.
It then rebuilds with value 0 and requires immediate successful load and unload.

Failed init performs no teardown because R02 owns no resource.
The kernel does not invoke the registered exit callback after failed initialization; this is a documented lifecycle contract, not something inferred from a learner-authored log.
Later checkpoints must unwind acquired resources inside init before returning an error.

## R03: owned heap state

R03 adds `include/rootkit.h` and `core/state.c` through additive inheritance.
It immediately uses both files; neither is a scaffold for future work.

The header exposes an incomplete type and the permanent state API:

```c
struct rk_state;

__must_check noinline struct rk_state *rk_state_create(void);
noinline void rk_state_destroy(struct rk_state *state);
```

`core/state.c` alone defines `struct rk_state` and owns its allocation.
`rk_state_create()` uses `kzalloc()`.
It converts allocator `NULL` to `ERR_PTR(-ENOMEM)` so callers receive one pointer-shaped result contract.
`rk_state_destroy()` is the only function that calls `kfree()` for that object.

`core/main.c` holds the returned pointer only to sequence lifetime.
It consumes the result with `IS_ERR()` and `PTR_ERR()`, preserves the original errno, and sets its local pointer to `NULL` immediately after destruction.
The private struct layout remains incomplete to consumers.

R03 extends the failure enum:

```c
RK_FAIL_STATE_ALLOC = 2,
```

Value 2 deterministically follows the same constructor path as `kzalloc()` returning `NULL`, producing `ERR_PTR(-ENOMEM)`.
The check requires the exact `-ENOMEM` loader failure, no module artifact, no observed state allocation, and a successful value-0 load/unload immediately afterward.

The ownership functions become `noinline` when introduced because M00 attributes allocator call sites to their exact ELF symbol ranges.

## R04: second resource and reverse rollback

R04 additively introduces `status/status.c` and immediately links and calls it.
The header gains the permanent status API:

```c
struct rk_status;

__must_check noinline struct rk_status *rk_status_create(const struct rk_state *state);
noinline void rk_status_destroy(struct rk_status *status);
```

`status/status.c` alone defines `struct rk_status`, owns its allocation, and may retain a non-owning state pointer.
State must therefore outlive status.

The concrete acquisition order is:

```text
preflight -> state -> status
```

Every failure and normal unload use the exact reverse order for resources that were successfully acquired:

```text
status -> state
```

`core/main.c` owns this ordering but never allocates or frees component storage itself.
Failed module initialization performs its unwind locally because `module_exit` is not called after failed init.

R04 adds:

```c
RK_FAIL_STATUS_CREATE = 3,
```

Value 3 makes `rk_status_create()` return `ERR_PTR(-EUCLEAN)` after state exists.
Init preserves `PTR_ERR()`, destroys state, and returns the original error.
The check requires exact rejection, absence of the module, independent evidence that the state allocation was freed, and immediate successful value-0 load/unload.

The status constructor and destructor are `noinline` from their first implementation for the same allocator-evidence reason as the state pair.

## Additive snapshot contract

The exact lineage is:

```text
R01 template: Kbuild, core/main.c
 `-- R01 passed snapshot
      `-- R02 workspace: inherit both paths unchanged in shape
           `-- R02 passed snapshot
                `-- R03 workspace: inherit both paths + add header/state
                     `-- R03 passed snapshot
                          `-- R04 workspace: inherit four paths + add status
                               `-- R04 passed snapshot
                                    `-- M00 workspace: inherit all five paths
                                         `-- M00 passed snapshot
```

An additive transition is explicit metadata, not inferred from directory contents.
It names the immediate provider and an immutable additions directory.
Materialization obeys these rules:

- verify the provider's passed snapshot and recorded hash before copying;
- copy every provider-declared file byte-for-byte with its executable class;
- require the consumer's inherited path set to equal the provider's path set;
- copy only newly declared paths from the immutable additions directory;
- reject a new path that already exists, a missing addition, a symlink, special
  file, undeclared extra, removal, rename, or changed build root;
- record provider stable ID and snapshot hash in learner state when the consumer
  workspace is first created;
- never rebase an existing consumer when its provider is rechecked or restored.

For a non-additive step, consumer and provider path sets are identical.
For an additive step, the consumer set is exactly the disjoint union of inherited paths and declared additions.

Reference solutions may be stored as replacement overlays containing only learner-edited or newly added files.
They are maintainer packaging, not learner baseline reconstruction and not an engine fallback when a required passed snapshot is absent.

Snapshot manifests remain content-intrinsic: canonical tree hash, declared relative paths, file hashes, and executable classes.
Curriculum revision, provider identity, and the chosen baseline hash belong to learner state.
Migration may retain a snapshot only through an explicit compatibility rule naming both stable IDs and certifying the path/build contract.

## M00: integrated evidence

M00 adds no file or API.
It uses the R04 completion with five declared paths: `Kbuild` plus four source/header files.
The learner completes and tests the concrete acquisition/unwind paths already introduced.

The check builds values 0 through 3 independently and requires:

- normal load, unload, absence, and immediate reload for value 0;
- exact rejection before acquisition for value 1;
- exact state allocation failure for value 2;
- exact status failure plus state rollback for value 3;
- module absence after every rejected load;
- no warning and compile-time rejection of unknown failure values.

## Independent allocator evidence

Module log messages are explanatory output only.
They are never accepted as proof that an allocation occurred or was freed.

The pinned guest exposes tracefs `kmem:kmalloc` and `kmem:kfree` events with `call_site=%pS` and `ptr=%p`; `kmalloc` also exposes `bytes_req` and `bytes_alloc`.
Despite the `%pS` declaration, repeated tests on the pinned guest rendered module call sites as raw `0xffffffffc...`
values both while loaded and after unload.
The `ptr` token is opaque and must not be described as a dereferenceable or disclosed kernel address.
Its textual value remains stable across paired allocation and free events and is suitable only for equality correlation within one trace run.

Before boot, the host checker reads the exact `rootkitlings.ko` ELF symbol table and `.text` section.
It records the `.text`-relative value and nonzero size of `rk_state_create`, `rk_state_destroy`, `rk_status_create`, and `rk_status_destroy`.
It rejects missing, overlapping, or wrong-section symbols.
Distinct ranges enforce the `noinline` contract.
The host renders those eight numeric constants into a temporary guest-check copy without modifying the learner workspace.

The guest check:

1. mounts tracefs, verifies the two event formats contain `call_site` and `ptr`,
   saves their enable state, disables them, and clears the trace;
2. enables `kmalloc`, loads the value-0 module, reads
   `/sys/module/rootkitlings/sections/.text`, and combines that runtime base
   with each host-derived offset and size;
3. stops tracing while the module remains loaded and requires one `kmalloc`
   raw call site inside each constructor range, recording its opaque nonzero
   pointer token;
4. disables `kmalloc`, enables `kfree`, clears the trace, unloads the module,
   disables tracing, and reads the free-phase trace;
5. requires one matching `kfree` token from the corresponding destructor range
   for each recorded allocation;
6. confirms module absence, removes checker captures, clears the trace, and
   restores the original event enable state.

This proves only that the two constructor-attributed tokens observed in that run were later passed to `kfree` from their expected destructor ranges.
It is not a general leak detector and does not prove absence of every possible kernel leak.

Tracepoint availability, required event fields, ELF symbol data, the loaded `.text` base, and parseable raw call sites are pinned-lab prerequisites.
Missing prerequisites fail runtime verification; they are not replaced by logs.
A static documentation job may report the runtime check as not run, but route completion may not.

## Status publication boundary

Stage 1 publishes no learner-authored procfs, sysfs, debugfs, parameter, or device interface.
The kernel-provided `/sys/module/rootkitlings` artifact is load evidence, not a status channel.
Procfs publication, permissions, and read semantics begin in Stage 2.

## Check safety

Checks build on the host but never insert a module into the host kernel.
Runtime checks use the disposable, diskless guest without networking or host mounts.
They restore trace event state, unload any successfully loaded module, remove temporary captures, and power down the guest even after a failed assertion.

## Source basis

This decision follows the official kernel documentation already collected for the curriculum:

- [Building external modules](https://docs.kernel.org/kbuild/modules.html)
- [Driver basics and module lifecycle](https://docs.kernel.org/driver-api/basics.html)
- [Kernel memory allocation](https://docs.kernel.org/core-api/memory-allocation.html)
- [Error pointers](https://docs.kernel.org/core-api/kernel-api.html#error-pointers)
- [Tracing guide](https://docs.kernel.org/trace/index.html)
