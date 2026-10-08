# Stage 1 implementation slice

Status: incubation design and R01 vertical slice.
Nothing here changes the public course route.

## Outcome

Stage 1 establishes a contained lab and then grows one inspectable, multi-file module with explicit ownership and rollback.
Each checkpoint changes learner owned state, has an independently observable success, and proves rejection or cleanup as well as the happy path.

| Unit | Learner-owned change | Observable success | Negative and cleanup evidence | Reused stable IDs |
|---|---|---|---|---|
| R01 Authorized lab | Complete the authorization, scope, stop, containment, and cleanup manifest | Diskless guest boots and reports the expected boundary | Incomplete or weakened scope is rejected before boot; no network or host mount; guest powers down and temporary state disappears | New material in `curriculum/incubator/stage-01/r01-authorized-lab` |
| R02 Module lifecycle | Write metadata and init/exit behavior in the permanent core | Module loads; metadata, symbols, `/sys/module`, inventory, and logs agree | Unload removes the sysfs/inventory artifact and records exit | `module-lifecycle`, `module-build` |
| R03 Owned components | Define the shared contract and split permanent `core` and `status` objects | Composite module links and exposes the intended component symbols | Omit one object or violate the header contract and the build fails without a load attempt | `multi-file-module` as a mechanism reference; its calculator scaffold is not reused |
| R04 Precise rejection | Add one controlled init rejection returning its original negative errno | Load reports the selected error and the module remains absent | Exit marker is absent after failed init; disabling injection permits a clean load | `init-failure` |
| R05 Reverse rollback | Implement top-level acquisition and reverse unwind for every acquired component | Normal load/unload succeeds repeatedly | Fail each acquisition point; no object remains, then immediate retry succeeds | `initialization-rollback` |
| R06 Heap and pointer failures | Move core state to owned heap storage and handle both `NULL` and pointer-shaped errors at their APIs | State is allocated, published only after initialization, and released on unload | Inject allocation failure and pointer error; preserve exact errno, publish nothing, invalidate released pointers, and retry cleanly | `kernel-allocation`, `error-pointer` |
| M01 Inspectable core | Integrate R02–R06 by writing the top-level acquisition table and unwind paths | Permanent multi-file module loads repeatedly and every component is statically inspectable | Inject every stage failure, repeat load/unload, and independently verify no module, sysfs object, allocation marker, or stale log claim remains | Solutions inform the contract but are not copied into a milestone scaffold |

## R01 implementation contract

The R01 template is intentionally not kernel source.
The learner must make a real decision about authorization and stop criteria before privileged code is introduced.
Its checker performs two phases:

1. Parse an exact TOML schema and reject placeholders, missing negative scope,
   fewer than two stop conditions, or any attempt to enable a network, host
   mount, persistent guest, or production data.
2. Boot the existing QEMU initramfs without a module, disk, or network.
   A guest
   check verifies the kernel interfaces used by later checks and rejects 9p,
   virtiofs, NFS, and the networking kernel command-line mode. QEMU shutdown and
   `vm/boot`'s temporary-directory trap provide cleanup evidence.

The example solution is a nonsensitive local-course declaration, not evidence of authorization for another environment.
A learner must not copy it as a substitute for permission.

## Cumulative source contract

R02 begins the permanent source tree; R03 changes it to this logical shape:

```text
Kbuild
include/rootkit.h
core/main.c
core/state.c
status/status.c
```

R04 and R06 add failure-producing operations behind the shared contract.
R05 owns the only top-level acquisition/unwind graph.
M01 does not start from a new template: it consumes the recorded completion of R06 and asks the learner to finish integration and failure testing.

The current stable-ID material remains the teaching reference.
Migration code must map completion snapshots forward; it must not infer a cumulative baseline from the current standalone exercise directories.

## Why an M01 scaffold is not included yet

A sound cumulative scaffold requires decisions not made by the migration map:

- the public component API and which object owns the shared state;
- the exact acquisition stages and stable failure-injection interface;
- whether status publication is deferred until Stage 2 or represented only by
  internal inspection in M01;
- which independent allocation evidence is available without adding a new
  debugfs/procfs interface too early;
- snapshot migration semantics from R02 through R06.

Inventing those choices in a source skeleton would make later checks dictate an accidental architecture.
Before authoring M01 assets, record an architecture decision for these five points, then implement R02–R06 as cumulative deltas and write M01 against the R06 completion snapshot.

## Acceptance tests before route adoption

- R01 template is rejected because placeholders remain.
- R01 example solution passes and the guest exits within its timeout.
- A manifest enabling networking, persistence, a host mount, or production data
  is rejected without invoking QEMU.
- Every future R02–R06 check has a positive control, a negative control, and an
  independent cleanup assertion.
- M01 injects each declared failure point individually and reloads immediately
  after every failure.
- The complete Stage 1 workspace builds with `W=1` and no warning.
- The public route remains unchanged until state migration and cumulative
  snapshot tests pass.
