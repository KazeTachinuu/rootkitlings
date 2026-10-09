# Rootkitlings curriculum migration map

Status: internal design decision draft.
This document governs the proposed redesign; the public README and current catalog remain authoritative until the migration is implemented.

## Promise

Learn Linux kernel development by building a rootkit one mechanism at a time.

The course is successful when a learner finishes with one rootkit they understand, can unload safely, can test under failure, and can detect through independent evidence.
It is not successful merely because the learner has sampled many kernel APIs.

## Decision

The target curriculum contains:

- 32 required mechanism lessons.
- 4 cumulative integration milestones.
- 1 final capstone.
- 8 optional depth tracks.

This produces 37 required checkpoints.
The number is a consequence of the dependency graph, not a quota.
It may change if implementation proves that two units cannot be taught or tested independently.

The current 54 lings and 11 capstones are not retained as a mandatory 65-step route.
No current material is deleted merely to reduce the count.
Material is merged, moved, refactored, or made optional according to its contribution to the course promise.

## What atomic means

A required lesson is atomic when it introduces one transferable kernel mechanism or one hard engineering invariant and gives the learner meaningful ownership of it.

Every required lesson must have:

- One precise learner-authored change.
- One rootkit subsystem that needs the result.
- One ownership, lifetime, context, or failure invariant.
- One observable success condition.
- One observable negative or cleanup condition.
- One later consumer in the cumulative project.

An individual API call is not automatically a lesson.
Conversely, a mechanism is not merged merely because its implementation is short.

## What a ling must teach

A ling is a small practical lesson, not an acceptance specification.
Its README must answer these questions in this order:

1. What changes in the kernel?
2. Why does the rootkit need this mechanism?
3. What small piece of code does the learner change?
4. What lifetime, failure, or safety rule applies?
5. What direct observation proves that it worked and cleaned up?
6. What independent observation can still reveal it?

Explain the mechanism before listing the task.
Define necessary kernel terms when they first appear.
Use one small diagram or code fragment when it makes the mechanism clearer.
Keep engine state, snapshot layout, future lesson sequencing, and maintainer test machinery out of learner-facing prose.

Combining several API calls is valid when they form one observable mechanism.
Build, insertion, initialization, removal, and exit therefore form one minimal module lifecycle.
Composite linking and a private state interface belong in the first lesson that uses heap state.
The procfs interface component belongs in the publication lesson, where the first real published resource makes reverse rollback observable and reusable.

## Course shape

```text
Course-entry diagnostic
|
+-- Authorized disposable-lab setup gate (mandatory, unscored)
|
+-- Stage 1: Module core
|   `-- R01-R04
|
+-- Stage 2: Control and resident execution
|   +-- R05-R08
|   `-- M00 Unify timed and requested work
|
+-- Stage 3: Identity, authorization and observation
|   +-- R09-R14
|   `-- M01 Authorized actions and telemetry
|
+-- Stage 4: Views, concealment and detection
|   +-- R15-R18
|   `-- M02 Concealment with an evidence map
|
+-- Stage 5: Networking and durable state
|   +-- R19-R27
|   `-- M03 Remote resident agent
|
`-- Stage 6: Authenticated operations
    +-- R28-R32
    `-- C00 Operation Glasshouse
```

The stages are learner-facing capability names.
API families such as procfs, concurrency, instrumentation, networking, and cryptography remain concepts and tags; they are not separate journeys that interrupt construction of the rootkit.

## Course-entry contract

Course-entry knowledge is declared once and checked by a diagnostic.
It is not repeated in every lesson.

- Shell navigation, permissions, processes, file descriptors, and exit status.
- C pointers, arrays, structures, callbacks, integer widths, and bounds.
- Counted byte buffers versus NUL-terminated strings.
- Linux PID, UID, GID, process, file, and device vocabulary.
- Host versus network byte order.
- Basic IPv4, ICMP, UDP, TCP, and DNS vocabulary.

The diagnostic should link directly to focused remediation.
It should not become a mandatory collection of introductory micro-lessons.

Before the required route, every learner must pass an unscored authorized-lab setup gate.
The gate records authorization, scope, stop conditions, reset, and cleanup evidence, then proves that the diskless guest exposes neither the host filesystem nor a network device.
It produces no course credit and teaches no kernel mechanism.
Its containment probe is `vm/checks/containment`; the learner records the local acknowledgement with `./rootkitlings setup --accept`.
Later networking checks may enable only capabilities declared by their exercise metadata; the gate verifies the safer default guest.

## Required route

Each `Requires` line names direct dependencies only.
Knowledge already reached through one of those dependencies is not repeated.

### Stage 1: Module core

#### R01 - Build, load, inspect, and unload a kernel module

- Teaches: external Kbuild, module metadata, init/exit, `insmod`, and `rmmod`.
- Produces: a loadable module with a complete outer lifecycle.
- Requires: the authorized disposable-lab setup gate.
- Defensive evidence: static metadata and lifecycle symbols before loading, then
  agreement between `/sys/module` and module inventory across load, unload, and
  immediate reload. Logs are diagnostics, not residency or cleanup evidence.
- Current sources: L00 and L01.

#### R02 - Reject initialization with a precise error

- Teaches: negative errno, propagation, failed-init semantics, the distinction
  between rejection and later teardown, and build-time failure injection through
  the first fixed failure point.
- Produces: a component with a deliberate, diagnosable activation failure.
- Requires: R01.
- Defensive evidence: numeric loader `errno`, explicit absence before and after
  repeated rejected loads, and an independent successful lifecycle regression.
  The rule that failed initialization does not call module exit is taught from
  the kernel contract rather than claimed from a log.
- Current source: L04.

#### R03 - Allocate and own private kernel state

- Teaches: composite Kbuild, a shared header with an incomplete private type,
  allocation lifetime, `NULL`, `ERR_PTR`, `IS_ERR`, `PTR_ERR`, and invalidation
  after release.
- Produces: a module linked from `core/main.o` and `core/state.o`, with dynamically
  owned private core state and an injected allocation failure.
- Requires: R02, including the build-time failure-injection contract.
- Defensive evidence: forced allocation failure, exact error propagation, clean
  retry, no stale published pointer, exact linked objects, and a private state
  layout.
- Current sources: L02, L07, and L12.

#### R04 - Publish and roll back a procfs entry

- Teaches: procfs publication lifetime and permission surface, acquisition order,
  single ownership, reverse unwind, pointer invalidation after release, and
  fail-at-each-stage testing using deterministic build-time failure injection.
- Produces: the real procfs interface component, its entry, and the top-level
  state-then-publication lifecycle and rollback graph used by every milestone.
- Requires: R03.
- Defensive evidence: the exact proc path and mode while loaded; agreement with
  module inventory; disappearance after unload or injected publication-boundary
  failure; trace-correlated release of state; repeated rejected loads; and an
  independent repeated successful lifecycle.
- Current sources: L08 and L09.

### Stage 2: Control and resident execution

#### R05 - Read module state through procfs

- Teaches: advancing file position, short reads, EOF, rewind, and the bounded read contract supplied by `seq_file`.
- Produces: a read-only `active=1` record derived from private module state.
- Requires: R04, including the component boundary and proc-before-state teardown order.
- Learner change: add one state accessor, pass the state pointer through `proc_create_single_data()`, and render one local value in the show callback.
- Lifetime: the state pointer is borrowed.
  `proc_remove()` prevents new callbacks, waits for active callbacks, and releases open sessions before state destruction.
  The immutable value needs no lock.
- Defensive evidence: byte-exact output including the newline; advancing partial reads; repeated EOF; rewind; rejected writes without mutation; unload with an open descriptor; path and inventory removal; and immediate reload.
- Current source: L10 contributes read-contract tests only.
  Its separate manual read callback is not carried into the cumulative `seq_file` design.

#### R06 - Enable and disable module actions through procfs

- Teaches: bounded userspace copy, copy-validate-commit, exact `enable` and
  `disable` commands, one-copy Boolean access with `READ_ONCE()` and
  `WRITE_ONCE()`, and choosing the invariant that the switch protects.
- Produces: a root-only control switch consumed by later module actions.
- Requires: R05.
- Defensive evidence: exact state reads before and after valid commands;
  rejection of empty, oversized, partial, trailing, embedded-NUL, and unknown
  inputs without state mutation; and concurrent toggles that preserve the
  binary state invariant.
- Scope: the switch is independent Boolean state, so concurrent writers may use
  last-writer-wins semantics without coupling it to another field.
  `READ_ONCE()` and `WRITE_ONCE()` force one compiler-visible scalar access;
  they are not atomic read-modify-write operations or a general ordering primitive.
- Current sources: L11 supplies the userspace boundary and C00 supplies the fixed
  control use case.

#### R07 - Defer timer work to a worker

- Teaches: timer callbacks cannot sleep; the callback reads the Boolean gate,
  advances its single-writer `unsigned long` counter, and queues work; the worker
  performs sleep-capable work and advances its own single-writer counter;
  publication, synchronous cancellation, and flush ordering close both lifetimes.
- Concurrency: readers use `READ_ONCE()` for the gate and counters.
  No learner-authored atomic read-modify-write operation is needed because each
  counter has exactly one writer.
- Produces: a periodic sleep-capable scheduler.
- Requires: R06.
- Defensive evidence: measured ticks, no work after unload, and unload while work is
  pending.
- Current sources: L17 and L18.

#### R08 - Run and stop an event-driven kernel thread

- Teaches: thread creation and pointer errors, wait queues, condition-loop waiting,
  the first genuine atomic read-modify-write operation, counted non-coalescing
  event admission, publish-before-wake, stop checks, wakeup, join, and the local
  rule that the thread may sleep between events.
- Admission: `atomic_add_unless(..., INT_MAX)` accepts every request until the
  counter saturates and returns `-EOVERFLOW` only at that bound.
  The thread consumes one accepted request per successful
  `atomic_dec_if_positive()`.
  Successful-run accounting is preserved exactly unless stop or unload begins.
- Produces: the resident worker.
- Requires: R06.
- Defensive evidence: sequential, burst, and concurrent request totals agree
  exactly with successful submissions and consumed events; stop while blocked
  completes; and the task disappears after unload.
- Current sources: L19, L20, and L23; L12's real pointer-error application is
  reinforced here.

#### M00 - Unify timed and requested work

Merge the useful integration work of C00 and C01.
The Boolean control switch gates both producers.
The timer callback and a fixed proc command submit to one capacity-one atomic request path, and the resident thread consumes admitted requests.
This deliberately refactors R08's counted non-coalescing queue: a periodic producer must coalesce demand instead of accumulating a growing work backlog.
The integrated result has no workqueue: L06 taught timer-to-workqueue deferral as an isolated callback-boundary mechanism, while M00 reuses the bounded timer producer and gives all sleep-capable work to the already-owned thread.
A request that observes disabled is not admitted: procfs `run` returns `EACCES`, while timer callbacks continue to be counted and ignore the same error.
Unload closes the timer, request, wake, and thread lifetimes cleanly.
Requires R07 and R08 directly.
M00 starts from the learner's passed R04 cumulative snapshot.
R06-R08 are separate practice workspaces, so the learner applies their mechanisms to that project; the engine does not merge those workspaces automatically.

### Stage 3: Identity, authorization and observation

#### R09 - Attribute a proc read to the executing task

- Teaches: `current`, stable snapshots of PID and command name, and why the process
  executing a callback is the caller.
- Produces: attributable control and event records.
- Requires: R05.
- Defensive evidence: independent comparison with the actual reader or writer.
- Current source: L24.

#### R10 - Interpret the current task's credentials

- Teaches: real, effective, saved, and filesystem IDs; capabilities; namespace-aware
  ID translation; the namespace that scopes a capability set; and read-only
  credential access.
- Produces: an authorization-context snapshot.
- Requires: R09.
- Defensive evidence: reads under distinct ID roles, a credential change after
  open, a nested user namespace, and a descriptor opened in one namespace but
  read in another. The last case distinguishes the current task supplying the
  credential from the opener namespace supplying the numeric ID view.
- Current source: L25.

#### R11 - Replace the writing task's credentials as one transaction

- Teaches: constructing a complete private credential from `init_task`, handling
  preparation failure, consuming it exactly once through commit or abort, and
  the same-caller effect of `commit_creds()`.
- Produces: one bounded action that replaces the writing task's entire
  credential without manually mutating UID, GID, capability, namespace,
  accounting, keyring, or security fields.
- Requires: R06 and R10.
- Defensive evidence: malformed or disabled commands have no effect; the exact
  writing thread receives one internally consistent initial credential while an
  unrelated process and a pre-existing sibling thread do not; preparation and
  post-preparation cancellation failures leave the caller unchanged.
- Current source: L26.

#### R12 - Authorize one credential change with a fresh token

- Teaches: kernel-random readiness, a fixed 128-bit bearer token, authorization
  versus authentication, exact byte-oriented hex parsing, consume-before-action,
  rotation after either action outcome, replay rejection, explicit-zeroing limits,
  and one mutex protecting the complete multi-field transition.
- Produces: a one-use bearer authorization protecting R11.
  The status endpoint is
  root-readable and writable by the selected unprivileged caller; this controls
  token acquisition through that path but does not authenticate whoever presents
  a copied token.
- Requires: R11.
- Defensive evidence: malformed, wrong, disabled, stale, reused, and current
  tokens are distinguished without state drift; many callers racing one token
  produce exactly one accepted transition; a matched token is consumed and
  replaced even when credential preparation or the post-prepare action fails.
- Current sources: L28 and the state machine in C03.

#### R13 - Manage two named Kprobes

- Teaches: exported versus internal symbols, configuration gates, kernel-version
  and architecture coupling, named Kprobe resolution, bounded handlers, managed
  hook-set lifetime, and rollback.
- Produces: entry telemetry through the supported `.symbol_name` registration path;
  learner code does not copy, call through, log, or publish the address retained
  inside each managed `struct kprobe`.
- Requires: R06, including proc publication, resource rollback, and one-copy shared
  scalar access.
- Learner change: define two named entry probes in one instrumentation component,
  update only bounded monotonic seen flags in their handlers, and publish and remove
  the set with `register_kprobes()` and `unregister_kprobes()`.
- ABI contract: the checker derives the release, architecture, configuration, symbol
  presence, and export surface from the exact kernel build used by the guest.
  Internal targets are never treated as a stable module API.
- Defensive evidence: both probes appear in the probe registry only while loaded;
  controlled calls preserve normal behavior and flip the matching flags; a real
  missing-second-symbol failure returns `-ENOENT` and leaves no first probe; proc
  publication failure removes the complete set; unload and immediate reload leave
  no stale probe-registry entry or module-published address.
- Current sources: L27 contributes the export-versus-internal distinction only;
  L29 and L33 contribute the real named-probe and hook-set lifecycle.

#### R14 - Correlate an rmdir path with its result

- Teaches: kretprobe entry/return state, `maxactive` and missed instances, syscall
  wrapper registers, `nofault-user-copy`, exact bounded path matching, and
  architecture limitations.
- Produces: success/failure telemetry for one exact `rmdir()` path.
- Requires: R13.
- Defensive evidence: per-invocation return correlation under concurrency, exact
  and near-match paths, hostile userspace pointers, ordinary calls unchanged,
  missed-instance reporting, missing-target rollback, and probe-registry cleanup.
- Current sources: L30 and L32.

#### M01 - Authorized actions and telemetry

Merge C03 and C04.
Add the one-use same-caller privilege action and a bounded event ring for selected file activity.
Spinlock ownership and ring overwrite semantics must be taught before or within a tightly scoped guided part of this milestone; they must not appear as unexplained scaffold magic.

### Stage 4: Views, concealment and detection

#### R15 - Distinguish an object from a rendered view

- Teaches: underlying object, enumeration path, direct lookup, positive and negative
  controls, cross-view comparison, and independent evidence.
- Produces: the evidence model used by every concealment mechanism.
- Requires: R05 and R13.
- Defensive evidence: a view can disagree while the object continues to exist.
- Current source: currently implicit across L34-L39; make it explicit.

#### R16 - Alter one module view without claiming invisibility

- Teaches: module-list membership, sysfs/kobject artifacts, reversible unlink/relink,
  intrusive kernel lists, and exact restoration.
- Produces: controlled module enumeration suppression.
- Requires: R15.
- Defensive evidence: module inventory disagrees with sysfs and other artifacts.
- Current sources: L34 and the useful evidence/lifecycle part of L35.

#### R17 - Filter packed directory records safely

- Teaches: validation of minimum and full record extent, name termination, forward
  progress, overlap-safe compaction, and returned-length correction.
- Produces: reusable directory-enumeration filtering.
- Requires: R14 and R15.
- Defensive evidence: direct lookup, unrelated directories, malformed records, and
  restoration after unload.
- Current source: L36.

#### R18 - Filter a bounded rendered result

- Teaches: one general read-result/row-filtering invariant and why each reporting
  path is only one view.
- Produces: a reusable bounded content filter.
- Requires: R14 and R15.
- Defensive evidence: underlying bytes or object remain independently visible.
- Current source: L37 as the required implementation; L38 and L39 become comparative
  optional labs.

#### M02 - Concealment with an evidence map

Refactor C05 into the cumulative project.
Hide the resident worker PID from proc root enumeration while preserving direct lookup, task evidence, scheduling, and control.
The milestone produces a machine-readable evidence card stating which view changed and which views did not.

### Stage 5: Networking and durable state

#### R19 - Parse hostile packet lengths before touching payload

- Teaches: network byte order, IPv4 IHL and total length, fragment policy, transport
  boundaries, overflow-safe arithmetic, and truncation.
- Produces: host-tested protocol parsers and fixtures.
- Requires: basic IPv4 packet knowledge declared as entry knowledge.
- Defensive evidence: malformed and truncated fixture corpus.
- Current sources: concepts are spread across L41, L42, L44, and L46 scaffolds.

#### R20 - Read an `sk_buff` without assuming linear storage

- Teaches: data/head/tail/end geometry, header offsets, nonlinear/shared storage,
  safe header access, and bounded copy helpers.
- Produces: a safe packet-view adapter.
- Requires: R19.
- Defensive evidence: linear, nonlinear, truncated, and fragmented cases.
- Current source: missing explicitly; current packet lessons hide part of this work.

#### R21 - Register a non-disruptive Netfilter observer

- Teaches: hook points, namespaces, priorities, verdicts, registration lifetime, and
  accept-by-default behavior, plus the local callback rule: do bounded,
  non-sleeping work and defer anything that may block.
- Produces: a packet observation point.
- Requires: R20 and R04.
- Defensive evidence: hook inventory/counters, unchanged traffic, and removal.
- Current source: L40.

#### R22 - Classify only fully validated packets

- Teaches: layered protocol predicates, exact matching, fragment policy, and
  malformed-input state preservation.
- Produces: bounded IPv4 and ICMP/transport classification.
- Requires: R21.
- Defensive evidence: matching, nonmatching, truncated, fragmented, and unrelated
  traffic.
- Current sources: L41 and the predicate work of L42.

#### R23 - Mutate a writable packet and repair its checksum state

- Teaches: copy-on-write, reacquiring pointers, length consistency, checksum and
  offload metadata, and narrow scope.
- Produces: one controlled packet redirection.
- Requires: R22.
- Defensive evidence: exact target is redirected; unrelated traffic is unchanged.
- Current source: L44.

#### R24 - Own a kernel socket from creation through release

- Teaches: namespace-aware creation, connect/send/receive errors, datagram exactness,
  shutdown, release, visibility in socket tables, and the local rule that
  blocking socket operations run only from an owned sleep-capable path.
- Produces: a kernel-owned endpoint.
- Requires: R04.
- Defensive evidence: socket inventory and disappearance after failure or unload.
- Current source: L43, refactored so the title and implementation agree on the
  actual socket family and type.

#### R25 - Frame and cancel a TCP client correctly

- Teaches: byte-stream framing, partial I/O, oversize-frame draining, retry/backoff,
  blocked-I/O cancellation, and one socket owner.
- Produces: the fixed-function resident agent transport.
- Requires: R08 and R24.
- Defensive evidence: split/coalesced frames, reconnects, malformed commands,
  unload while blocked, and socket cleanup.
- Current source: the transport work in C07.

#### R26 - Encode and decode a bounded DNS wire name

- Teaches: length-prefixed labels, terminal root, exact 63/255 bounds, compression
  policy, and failure without prior-output mutation.
- Produces: a DNS-shaped message component.
- Requires: R19.
- Defensive evidence: boundary vectors and malformed names.
- Current source: L47.

#### R27 - Apply and conditionally roll back a file transaction

- Teaches: exact original image, bounded kernel file I/O, foreign-file ownership,
  compare-before-restore, and the local rule that file I/O runs only from an
  owned sleep-capable path with no conflicting lock held.
- Produces: a guarded durable boot-request edit.
- Requires: R04.
- Defensive evidence: byte-for-byte restoration, sentinel protection, preservation
  of newer foreign content, and file-integrity monitoring.
- Current source: L48.

#### M03 - Remote resident agent

Merge C07 and C08 into the cumulative project.
The module connects only inside the declared lab network, accepts a small fixed command set such as `PING`, `INFO`, and a harmless bounded marker, exposes retry/error counters, unloads while blocked, and manages only its exact boot request.
This milestone does not expose a general shell.

### Stage 6: Authenticated operations

#### R28 - Choose the protection property before the primitive

- Teaches: encoding, XOR obfuscation, confidentiality, integrity, authentication,
  freshness, replay, and traffic-analysis limits.
- Produces: a threat-to-primitive decision table.
- Requires: R26.
- Defensive evidence: plaintext recovery and static/runtime artifact comparison.
- Current source: L49 becomes one demonstration inside this lesson, not a claim of
  cryptographic protection.

#### R29 - Compute and verify SHA-256 correctly

- Teaches: Kernel Crypto API `shash` lifecycle, descriptor sizing, known-answer
  vectors, comparison, and why a public digest is not authentication.
- Produces: integrity verification against a trusted fingerprint.
- Requires: R28.
- Defensive evidence: known-answer accept/reject cases and keyless forgery model.
- Current source: L51.

#### R30 - Authenticate a message and reject replay

- Teaches: HMAC, key ownership, tag truncation budget, constant-time comparison,
  sequence/challenge state, and fail-closed updates.
- Produces: authenticated fixed commands.
- Requires: R12 and R29.
- Defensive evidence: known-answer tags, tampering, wrong key, stale sequence,
  duplicate sequence, and state unchanged on failure.
- Current source: L52 plus missing replay policy.

#### R31 - Use AEAD without releasing unauthenticated plaintext

- Teaches: transform/request/scatterlist lifetime, AAD, unique nonces, tag layout,
  asynchronous completion, `-EBADMSG`, and secret-erasure limits.
- Produces: an optional confidential transport envelope.
- Requires: R30.
- Defensive evidence: known-answer vector, nonce discipline, tag/ciphertext tamper,
  and no plaintext consumption on failure.
- Current source: L50, moved after the simpler hash and MAC property progression.

#### R32 - Emit an authenticated DNS-shaped signal

- Teaches: composition of bounded DNS encoding, HMAC/freshness, socket ownership,
  and a paired detection hypothesis.
- Produces: one local authenticated status signal, not a general command tunnel.
- Requires: R15, R24, and R30.
- Defensive evidence: independent packet decode and tag verification, replay
  rejection, and documented DNS anomaly indicators.
- Current sources: C09 and the bounded keyed-label idea from L53.

#### C00 - Operation Glasshouse: validate a rootkit through independent views

The only final capstone extends the learner's own M03 project.
It does not provide a fresh collection of duplicate implementations.

The learner must:

- Wire existing lifecycle, control, telemetry, privilege, concealment, networking,
  durable state, and authenticated signaling into one acquisition/unwind graph.
- Demonstrate fixed authorization and authenticated control behavior.
- Induce one failure, concurrency, cancellation, or tamper case per subsystem.
- Find every feature through at least one independent defensive view.
- Explain every deliberate disagreement between two views.
- Prove unload and rollback leave no owned object, worker, hook, socket, or file edit.

Current source: C10, with C03, C05, C08, and C09 functionality reused rather than reimplemented.

## Optional depth tracks

Optional work never blocks the required route and never silently mutates the required cumulative baseline.

### E01 - Kernel debugging

- QEMU/GDB, relocation, breakpoints, crash triage, and symbols.
- Current source: L03.

### E02 - Alternate binary control ABI

- Character devices, versioned ioctl structures, padding, fixed-width fields, and
  compat handling.
- Current sources: L14 and L15.

### E03 - Fixed userspace-helper boundary

- Fixed argv/env, wait policy, process evidence, and lifecycle ownership.
- Never accepts arbitrary shell text.
- Current source: L16, with its false proc-write prerequisite removed unless the
  exercise is explicitly redesigned around a proc trigger.

### E04 - Advanced concurrency and diagnostics

- Completions, watchdog/liveness state, lock ordering, memory ordering,
  reference counting, lockdep, KCSAN, KASAN, kmemleak, and fault injection.
- Current sources: L21 and C02; selected L20/L23 material is already required.

### E05 - Alternate instrumentation

- Fprobe, ftrace, tracepoints, and defensive BPF comparison.
- Current source: L31.

### E06 - Alternate views and hidden storage

- Kernel-log filtering, `/proc/net` row filtering, and the sealed dead-drop plugin.
- Every lab names the single altered view and the independent evidence that remains.
- Current sources: L38, L39, and C06.

### E07 - Alternate packet channels and helper handoff

- ICMP payload carriage, raw ICMP endpoints, and controlled descriptor handoff.
- A plaintext TCP root shell may be analyzed in the isolated guest, but it is not a
  required product and is never presented as authenticated control.
- Current sources: L42, L43's raw-specific material, L45, and L46.

### E08 - Cryptographic and rendezvous extensions

- Static XOR artifact analysis, AEAD confidentiality upgrade, keyed time-bucket
  labels, NXDOMAIN behavior, sinkholing, and defender detection.
- Current sources: L49, L50, and L53 after their core concepts are introduced in
  R28, R31, and R32.

## Disposition of every current item

This table is exhaustive.
`Merge` means that the mechanism remains, not that the old exercise identity remains.
`Refactor` means the current implementation is a substantial input but the target contract changes.

| Current | Decision | Target | Reason |
|---------|----------|--------|--------|
| L00 | Merge | R01 | Lifecycle and load loop belong with build and inspection. |
| L01 | Merge | R01 | A second no-code onboarding checkpoint is artificial. |
| L02 | Merge | R03 | Composite linking and the private header are taught with the heap state that uses them. |
| L03 | Optional | E01 | High-value tool, but not a rootkit capability dependency. |
| L04 | Refactor | R02 | Preserve exact failed-init semantics before rollback. |
| L05 | Retire | — | Module-parameter state has no required downstream consumer; R06 owns the action switch. |
| L06 | Retire | — | Parameter callbacks duplicate the validated state transition taught through the required procfs control path. |
| L07 | Merge | R03 | Heap ownership belongs with real allocation error contracts. |
| L08 | Merge | R04 | Reverse rollback is taught through the real procfs resource that needs it. |
| L09 | Refactor | R04 | Publication lifetime supplies the concrete second-resource lifecycle. |
| L10 | Refactor | R05 | Teach the complete read contract, preferably with framework support. |
| L11 | Keep/refactor | R06 | Critical userspace trust-boundary transaction. |
| L12 | Merge | R03/R08 | Pointer errors are taught where allocation and kthread creation return them. |
| L13 | Merge | R16 | Only the intrusive-list mechanics survive, where module-list manipulation consumes them. |
| L14 | Merge | E02 | Device registration alone is an incomplete optional ABI. |
| L15 | Merge | E02 | Valuable alternate typed control plane, not required for the proc-based core. |
| L16 | Move/optional | E03 | Unique boundary; current proc prerequisite is false for its task. |
| L17 | Merge | R07 | Timer exists to motivate context deferral. |
| L18 | Merge | R07 | Workqueue completes the timer/context mechanism. |
| L19 | Merge | R08 | Resident thread plus wait/stop lifecycle is the meaningful unit. |
| L20 | Merge | R08 | Atomic read-modify-write implements counted bounded event admission. |
| L21 | Optional | E04 | Completion is useful but has no required downstream consumer. |
| L22 | Merge | R12 | Mutex protection is taught where a real multi-field one-use invariant needs it. |
| L23 | Merge | R08 | Wait queue is part of the event-driven worker lifecycle. |
| C00 | Merge | R06/M00 | Its bounded fixed control is taught in R06 and integrated with resident actions in M00. |
| C01 | Merge | M00 | Timer scheduling feeds the unified capacity-one request path. |
| C02 | Optional | E04 | Strong concurrency synthesis, but not consumed by the required route. |
| L24 | Refactor | R09 | Caller attribution remains explicit before credentials. |
| L25 | Refactor | R10 | Credential vocabulary and namespaces require their own contract. |
| L26 | Refactor | R11 | Transactional same-caller credential change is core. |
| L27 | Merge | R13 | Its export-versus-internal distinction moves into the real named-probe lifecycle; the temporary raw-address resolver is retired. |
| L28 | Merge | R12 | Randomness is taught through the one-use authorization state that needs it. |
| C03 | Merge | M01 | Integrates privilege and one-use authorization into the cumulative rootkit. |
| L29 | Merge | R13 | Named entry probing, ABI pinning, and hook-set rollback form one managed lifecycle. |
| L30 | Merge | R14 | Return correlation remains required for later view filters. |
| L31 | Optional | E05 | Fprobe is an alternate instrumentation API, not a new capability. |
| L32 | Merge | R14 | Syscall ABI and no-fault argument copying complete return-aware probing. |
| L33 | Merge | R13 | Partial hook rollback is essential but too thin as a separate task. |
| C04 | Merge | M01 | Telemetry joins the authorized-action milestone. |
| L34 | Refactor | R16 | Precise one-view suppression with independent evidence. |
| L35 | Merge | R16 | Sysfs artifacts provide the necessary cross-view and lifecycle evidence. |
| L36 | Refactor | R17 | Central variable-record filter reused by process concealment. |
| L37 | Refactor | R18 | Representative required rendered-result filter. |
| L38 | Optional | E06 | Comparative read-view variant with no core consumer. |
| L39 | Optional | E06 | Comparative seq-table variant with no core consumer. |
| C05 | Refactor | M02 | Strong cumulative concealment and cross-view milestone. |
| C06 | Optional | E06 | Duplicates control storage, filtering, and trigger mechanisms. |
| L40 | Refactor | R21 | Netfilter lifecycle remains explicit after skb preparation. |
| L41 | Merge | R22 | Classification is the useful result of packet observation. |
| L42 | Merge/optional | R22/E07 | Predicate is core parsing; ICMP channel variant is optional. |
| L43 | Refactor/split | R24/E07 | Generic socket ownership is core; raw ICMP specificity is optional. |
| L44 | Refactor | R23 | Distinct active mutation and checksum invariant. |
| L45 | Optional | E07 | Fixed-function agent is safer and pedagogically richer than a required shell. |
| L46 | Optional | E07 | Alternate ICMP carrier after required validated parsing. |
| L47 | Refactor | R26 | Strong bounded protocol encoder used by authenticated signaling. |
| C07 | Refactor | R25/M03 | Transport mechanics become a lesson; integration extends the learner's project. |
| L48 | Refactor | R27 | Guarded reversible file transaction remains core. |
| C08 | Merge | M03 | Boot request joins the remote resident milestone; do not claim a reboot test that does not run. |
| L49 | Merge/optional | R28/E08 | XOR is an honest obfuscation demonstration, never encryption. |
| L50 | Move/refactor | R31/E08 | AEAD follows hash and HMAC property progression. |
| L51 | Refactor | R29 | Public digest precedes keyed authentication. |
| L52 | Refactor | R30 | Add replay state and fail-closed update semantics. |
| L53 | Merge/optional | R32/E08 | Keyed time-bucket label is applied to the authenticated signal and defender analysis. |
| C09 | Refactor | R32 | Required bounded authenticated signal; no general DNS command tunnel. |
| C10 | Refactor | C00 | Only final capstone; reuse the learner's cumulative implementation. |

## Concepts currently missing or under-taught

These gaps explain why migrating the current 65 items directly into 32 mechanism lessons would be incomplete.
They must be added, made explicit, or consciously excluded.

- Callback-specific blocking, allocation, and synchronization constraints where
  each timer, probe, packet, socket, or file path is introduced.
- Spinlocks and non-sleeping synchronization before the event ring.
- Memory ordering and publish-before-wake.
- Reference lifetime versus locking.
- Lock ordering, deadlock reasoning, and lockdep.
- KASAN, KCSAN, kmemleak, and fault-injection evidence.
- Stable UAPI layout and ioctl compatibility in the optional ABI track.
- Kernel API, configuration, symbol, and architecture fragility.
- `sk_buff` geometry, nonlinear data, ownership, and writable conversion.
- Network byte order, fragmentation policy, and checksum ownership.
- Credential namespaces and capabilities.
- Crypto key, nonce, replay, and secret-lifetime limits.
- Module signing, lockdown, taint, and defensive module inventory.
- RCU before any future lockless shared lookup or hook-table design.

RCU is not forced into the core merely to claim completeness.
It becomes required only if the final implementation actually relies on a read-mostly shared object whose lifetime cannot be expressed safely with the core locking model.

## Metadata model

There must be one authoritative registry.
Do not duplicate sequence numbers, section identity, solution paths, checker paths, and display order across files.

Recommended shape:

```toml
schema_version = 2
id = "directory-record-filter"
title = "Filter packed directory records"
kind = "lesson"                 # lesson | milestone | capstone
status = "required"             # required | elective
difficulty = "advanced"
prerequisites = ["return-observation", "object-versus-view"]
teaches = ["directory-record-filtering"]
requires = ["kretprobe-correlation", "bounded-record-parsing"]
tradecraft = ["concealment", "detection"]

[workspace]
mode = "single-file"            # single-file | project | cumulative
template = "curriculum/directory-record-filter"
baseline = "template"           # template | previous-required | cumulative-root | previous-milestone
build = "."

[verification]
timeout_seconds = 90
capabilities = []
tools = []
cleanup_required = true
```

Course-level metadata owns:

- Required route and elective-track membership.
- The stable ID of the cumulative project root.
- Generated display serials (`L00`, `M00`, and `C00`).
- Stage registry and order.
- Concept registry and labels.
- Course-entry competencies.
- Difficulty rubric.
- Tradecraft tag vocabulary.
- Runtime capability registry.
- Supported architecture and kernel contract.

Exercise metadata owns only exercise-specific facts.
Internal stable IDs, not display sequence positions, are stored in learner state.
CLI selectors may accept both the generated serial and stable ID.

### Tradecraft tags

Tradecraft is a cross-cutting classification, not another directory tree.

- `lifecycle`
- `control`
- `execution`
- `privilege`
- `instrumentation`
- `concealment`
- `networking`
- `persistence`
- `cryptography`
- `detection`

Tags are added only when they drive filtering, display, or validation.
They must not restate `teaches` with different spelling.

## Learner workspace, restoration, and progress reset model

Canonical templates and references are immutable during normal learner work.

```text
.rootkitlings/
+-- state.toml
+-- work/<stable-id>/
+-- passed/<content-hash>/
`-- archive/<stable-id>-<content-hash>/

curriculum/
+-- templates/<stable-id>/
+-- support/<stable-id>/
+-- solutions/<stable-id>/
`-- incubator/<stage>/
```

- `start ID` materializes a workspace once and records its baseline hash.
- R02 through R04 use `previous-required` to extend the immediately preceding required pass without storing parent IDs.
- Course metadata declares R04 as the cumulative root; M00 starts from its exact passed snapshot.
- Each later cumulative milestone starts from the exact previous passed milestone.
- `check ID` builds only that workspace and records build, boot, and test results.
- A pass creates an immutable content-addressed snapshot.
- Rechecking history never silently rebases later work.
- `restore ID` archives current work and restores its recorded starting baseline.
- `clean ID` removes build output only.
- `progress-reset ID` archives affected work and resets that checkpoint and its
  dependents; `progress-reset --all` explicitly resets all learner progress.

State records stable IDs and completed IDs, not a numeric cursor.
All state writes must be atomic.
A curriculum reorder must not change the meaning of saved progress.

## Verification contract

Current live QEMU checks are a strong foundation.
Preserve fresh guests, real module loading, negative inputs, teardown assertions, and cross-view evidence.

Improve the contract as follows:

- Separate build, boot-ready, and exercise-test timeouts.
- Return a structured machine result instead of scraping a success sentence.
- Copy only metadata-declared guest tools.
- Generate runtime capability setup from one capability registry.
- Add reusable assertions for kernel faults, probe removal, module removal, paths,
  eventual state, and exact key/value parsing.
- Test unload while asynchronous work, waits, sockets, or callbacks are active.
- Offer `validate --quick`, build validation, and full reference-runtime validation.
- Publish a pinned kernel image, modules, and build bundle for reproducibility.
- Retain host-kernel mode only as an explicit developer option.

Passing proves the exercised observable contract.
It does not prove mastery, race freedom under every schedule, absence of all leaks, or assessment integrity.

## Time estimates

Do not add invented `estimated_minutes` values.

Until learner evidence exists, expose only a documented difficulty rubric based on:

- Conceptual novelty.
- Amount of learner-authored code.
- Number of ownership and failure paths.
- Debugging surface.

If timing is later collected, it must be opt-in and separate active learner time from build, VM boot, and test time.
Publish rounded quartile ranges only after a meaningful cohort has completed the same revision, and separate solution-revealed attempts from the primary sample.

## Naming rules

- Titles describe the observable learner outcome, not merely an API noun.
- Claims name their scope: one list, one read path, one socket table, one guest.
- “Hidden” never means undetectable.
- “Authenticated” requires a key plus freshness/replay policy, not a hash alone.
- “Persistent” states exactly what survives; a written boot request is not a proven
  reboot until a second-boot test exists.
- “Covert” is avoided unless the lesson defines the observer and detection model.
- Project names may be memorable, but catalogs include a descriptive subtitle.

## Implementation order

1. Preserve the current work in a reviewed checkpoint before structural migration.
2. Implement stable-ID state, isolated workspaces, restoration, and migration-free v2
   metadata foundations.
3. Make the route, serials, stages, concepts, tags, checks, and references derive
   from the single registry.
4. Implement the mandatory unscored authorized-lab setup gate before exposing
   the required route.
5. Build R01-R08 and M00 first; validate the complete novice-to-resident-core
   journey before migrating later stages.
6. Migrate identity, probes, and visibility with explicit context and cross-view
   contracts.
7. Add host-side packet fixtures and explicit skb teaching before networking work.
8. Reorder the crypto property progression and integrate authenticated operations.
9. Convert existing capstone code into cumulative milestone components.
10. Run all references through full runtime validation on the pinned kernel.
11. Remove the old 65-count assumptions, numeric state, editable canonical
    scaffolds, inferred workspace modes, duplicate paths, and obsolete directories.

No compatibility layer or parallel curriculum remains after the migration.
The repository should finish with one route, one schema, one state model, and one learner workspace model.
