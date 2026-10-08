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
| R04 Procfs entry and rollback | add `interface/proc.c` | Real procfs publication and reverse partial-init unwind |

R04 closes Stage 1 with integrated failure, publication, cleanup, reload, and allocator evidence.
There is no separate scored checkpoint that copies the unchanged R04 workspace and reruns the same checks.

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

R04 appends the real procfs interface component:

```make
rootkitlings-y := core/main.o core/state.o interface/proc.o
```

No earlier lesson ships an empty state/status translation unit, declares an unused ownership API, or asks the learner to implement a function that is replaced before it is consumed.

## R01: module build and lifecycle

R01 owns only `Kbuild` and `core/main.c`.
`core/main.c` registers successful init and exit callbacks and declares the GPL license and the stable `Rootkitlings kernel module` description.

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

__must_check struct rk_state *rk_state_create(void);
void rk_state_destroy(struct rk_state *state);
```

`core/state.c` alone defines `struct rk_state` and owns its allocation.
`rk_state_create()` uses `kzalloc()`.
It converts allocator `NULL` to `ERR_PTR(-ENOMEM)` so callers receive one return value.
`rk_state_destroy()` is the only function that calls `kfree()` for that object.

`core/main.c` holds the returned pointer only to sequence lifetime.
It checks the result with `IS_ERR()` and `PTR_ERR()`, preserves the original errno, and sets its local pointer to `NULL` immediately after destruction.
The private struct layout remains incomplete to other components.

R03 extends the failure enum:

```c
RK_FAIL_STATE_ALLOC = 2,
```

Value 2 deterministically follows the same constructor path as `kzalloc()` returning `NULL`, producing `ERR_PTR(-ENOMEM)`.
The check requires the exact `-ENOMEM` loader failure, no module artifact, no observed state allocation, and a successful value-0 load/unload immediately afterward.

## R04: procfs entry publication and reverse rollback

R04 additively introduces `interface/proc.c` and immediately links and calls it.
The header gains the permanent publication API:

```c
__must_check int rk_proc_publish(void);
void rk_proc_unpublish(void);
```

`interface/proc.c` alone owns the proc entry.
A supplied `proc_create_single()` show function returns the fixed line `active`, so the mode-`0444` entry is a working status endpoint rather than an unreadable marker.
R05 replaces that fixed status with a bounded snapshot of module state.
State exists before publication and outlives the proc entry so later operations never expose an interface backed by absent state.

The concrete acquisition order is:

```text
preflight -> state -> proc status
```

Every failure and normal unload use the exact reverse order for resources that were successfully acquired:

```text
proc status -> state
```

`core/main.c` owns this ordering but never allocates or frees component storage itself.
Failed module initialization performs its unwind locally because `module_exit` is not called after failed init.

R04 adds:

```c
RK_FAIL_PROC_PUBLISH = 3,
```

Value 3 selects `NULL` instead of calling `proc_create_single()` after state exists.
It then follows the same branch as a real `proc_create_single()` allocation failure and returns `-ENOMEM`.
Init preserves the selected error, destroys state, and returns it.
The check requires repeated exact rejection, absence of the module and proc entry, independent evidence that the state allocation was freed, and an independent value-0 publication, unload, disappearance, and reload cycle.

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
                               `-- R04 passed snapshot: Stage 1 cumulative root
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

## Stage 1 integrated evidence

R04 checks the complete five-path module rather than deferring integration to an unchanged follow-up workspace.
Its check builds values 0 through 3 independently and requires:

- normal load, unload, absence, and immediate reload for value 0;
- exact rejection before acquisition for value 1;
- exact state allocation failure for value 2;
- exact publication-boundary failure plus state rollback for value 3;
- module and proc-entry absence after every rejected load;
- no warning and compile-time rejection of unknown failure values.

## Independent allocator evidence

Module log messages are explanatory output only.
They are never accepted as proof that an allocation occurred or was freed.

The pinned guest exposes tracefs `kmem:kmalloc` and `kmem:kfree` events with `call_site=%pS` and `ptr=%p`.
After a rejected load removes the module's symbols, its call sites appear as raw `0xffffffffc...`
values while built-in kernel call sites remain symbolized.
The `ptr` token is opaque and must not be described as a dereferenceable or disclosed kernel address.
Its textual value remains stable across paired allocation and free events and is suitable only for equality correlation within one trace run.

Before boot, the host checker inspects each linked object.
It requires the state functions and the state object's allocation and free dependencies in `core/state.o`, requires `core/main.o` to call those functions, and rejects allocator or free dependencies in the other learner-owned objects.

The failure-point-3 guest check:

1. mounts tracefs, verifies the two event formats contain `call_site` and `ptr`,
   saves their enable state, disables them, and clears the trace;
2. enables both events and performs the rejected load;
3. confirms the exact error and absence of the module and proc entry;
4. filters to the numeric loader task and requires exactly one nonzero pointer
   token from a raw module allocation call site and the same token from exactly
   one raw module free call site;
5. removes checker captures, clears the trace, and restores the original event
   state.

Together with the object-file ownership check, this proves only that the module allocation observed in that rejected run was passed to `kfree` during rollback.
It is not a general leak detector and does not prove absence of every possible kernel leak.

Tracepoint availability, required event fields, object-file symbol data, and parseable raw module call sites are pinned-lab prerequisites.
Missing prerequisites fail runtime verification; they are not replaced by logs.
A static documentation job may report the runtime check as not run, but route completion may not.

## Procfs publication boundary

Stage 1 publishes a procfs entry so rollback has a concrete second resource and a kernel-maintained namespace observation.
It supplies only a fixed status line; bounded state snapshots and open-file behavior begin with the next lesson.
The kernel-provided `/sys/module/rootkitlings` artifact remains independent load evidence rather than the learner-authored status channel.

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
