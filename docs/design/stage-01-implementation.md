# Stage 1 implementation slice

Status: incubation design with R01 and R02 vertical slices.
Nothing here changes the public course route.

## Outcome

Stage 1 establishes a contained lab and then grows one inspectable, multi-file module with explicit ownership and rollback.
Each checkpoint changes learner owned state, has an independently observable success, and proves rejection or cleanup as well as the happy path.

| Unit | Learner-owned change | Observable success | Negative and cleanup evidence | Reused stable IDs |
|---|---|---|---|---|
| R01 Authorized lab | Complete the authorization, scope, stop, containment, and cleanup manifest | Diskless guest boots and reports the expected boundary | Incomplete or weakened scope is rejected before boot; no network or host mount; guest powers down and temporary state disappears | New material in `curriculum/incubator/stage-01/r01-authorized-lab` |
| R02 Module lifecycle | Write metadata and init/exit behavior in the permanent core | Module loads; static metadata, symbols, `/sys/module`, and module inventory agree | Unload removes the sysfs and inventory artifacts; an immediate reload succeeds | `module-lifecycle`, `module-build` |
| R03 Owned components | Define the shared contract and split permanent `core` and `status` objects | Composite module links and exposes the intended component symbols | Omit one object or violate the header contract and the build fails without a load attempt | `multi-file-module` as a mechanism reference; its calculator scaffold is not reused |
| R04 Precise rejection | Add one controlled init rejection returning its original negative errno | Loader reports the selected error and the module remains absent | `/proc/modules` and `/sys/module` stay absent; rebuilding without injection permits an immediate clean load | `init-failure` |
| R05 Owned kernel state | Implement state allocation, convert `kzalloc()`'s `NULL` to `ERR_PTR(-ENOMEM)`, and consume it with `IS_ERR()`/`PTR_ERR()` | State allocation is attributed to its constructor and released on unload | Inject allocation failure; preserve `-ENOMEM`, publish nothing, and retry cleanly | `kernel-allocation`, `error-pointer` |
| R06 Resource rollback | Add status allocation and own the final state-then-status acquisition and status-then-state unwind | Both owned allocations are attributed and released on normal unload | Fail status creation with `ERR_PTR(-EUCLEAN)`; state is freed, no module remains, and immediate retry succeeds | `initialization-rollback` |
| M00 Inspectable core (migration-map M01) | Integrate R02–R06 by writing the top-level acquisition table and unwind paths | Permanent multi-file module loads repeatedly and every component is statically inspectable | Inject every stage failure, repeat load/unload, and correlate each owned allocation's opaque pointer token with its tracefs free event | Solutions inform the contract but are not copied into a milestone scaffold |

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

The accepted [Stage 1 cumulative architecture](stage-01-architecture.md) resolves file ownership, APIs, build-time failure injection, status publication, allocator evidence, and snapshot compatibility.

R02 begins with the complete permanent source tree:

```text
Kbuild
include/rootkit.h
core/main.c
core/state.c
status/status.c
```

Inactive files begin as warning-free private stubs so the declared file set and build root never change across completion inheritance.
R04 introduces the build-time-failure-injection ABI with the preflight point.
R05 adds the state allocation point and pointer-error contract.
R06 adds the status point and owns the final top-level acquisition/unwind graph.
M00 does not start from a new template: it consumes the recorded completion of R06 and asks the learner to finish integration and failure testing.

The migration map's M01 is the first generated milestone and therefore has public serial M00.
It publishes no learner-authored status interface; procfs publication remains R07 in Stage 2.

The current stable-ID material remains the teaching reference.
Migration code must map completion snapshots forward; it must not infer a cumulative baseline from the current standalone exercise directories.

## M00 scaffold readiness

The former architecture blockers are resolved in the linked decision:

- `core/state.c` and `status/status.c` each own one concrete allocation;
- `core/main.c` owns only activation and reverse teardown order;
- R04 introduces the fixed build-time-failure-injection ABI, R05 adds state
  allocation failure, and R06 adds status failure and reverse unwind;
- status publication waits for the Stage 2 procfs lesson;
- tracefs assigns opaque pointer tokens to raw call sites inside precomputed
  constructor ranges, then correlates each token with a post-unload free inside
  its destructor range, independently of logs;
- R02 creates the fixed five-file tree and every later checkpoint inherits the
  immediately preceding passed snapshot through M00.

M00 source and checks may now be authored, but only together with the cumulative R02–R06 deltas.
A standalone M00 scaffold would bypass the completion lineage and remains prohibited.

## Acceptance tests before route adoption

- R01 template is rejected because placeholders remain.
- R01 example solution passes and the guest exits within its timeout.
- A manifest enabling networking, persistence, a host mount, or production data
  is rejected without invoking QEMU.
- Every future R03–R06 check has a positive control, a negative control, and an
  independent cleanup assertion.
- M00 injects each declared failure point individually and reloads immediately
  after every failure.
- The complete Stage 1 workspace builds with `W=1` and no warning.
- The public route remains unchanged until state migration and cumulative
  snapshot tests pass.
