# Rootkitlings curriculum migration map

Status: internal design decision draft.
This document governs the proposed redesign; the public README and current catalog remain authoritative until the migration is implemented.

## Promise

Learn Linux kernel development by building a rootkit one mechanism at a time.

The course is successful when a learner finishes with one rootkit they understand, can unload safely, can test under failure, and can detect through independent evidence.
It is not successful merely because the learner has sampled many kernel APIs.

## Decision

The target curriculum contains:

- 34 required mechanism lessons.
- 4 cumulative integration milestones.
- 1 final capstone.
- 8 optional depth tracks.

This produces 39 required checkpoints.
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
|   +-- R05-R09
|   `-- M00 Controlled resident core
|
+-- Stage 3: Identity, authorization and observation
|   +-- R10-R16
|   `-- M01 Authorized actions and telemetry
|
+-- Stage 4: Views, concealment and detection
|   +-- R17-R20
|   `-- M02 Concealment with an evidence map
|
+-- Stage 5: Networking and durable state
|   +-- R21-R29
|   `-- M03 Remote resident agent
|
`-- Stage 6: Authenticated operations
    +-- R30-R34
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

#### R01 - Build, load, inspect, and unload a module

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

#### R03 - Build and own private heap state

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
  `disable` commands, atomic state transitions, and choosing the invariant that
  the switch protects.
- Produces: a root-only control switch consumed by later module actions.
- Requires: R05.
- Defensive evidence: exact state reads before and after valid commands;
  rejection of empty, oversized, partial, trailing, embedded-NUL, and unknown
  inputs without state mutation; and concurrent toggles that preserve the
  binary state invariant.
- Current sources: L11 supplies the userspace boundary, L20 supplies the atomic
  scalar operation, and C00 supplies the fixed control use case.

#### R07 - Choose a legal execution context

- Teaches: process, timer/softirq, probe, and other atomic contexts; sleepability;
  faultability; allocation flags; and legal synchronization.
- Produces: the context decision table used by all asynchronous and hook code.
- Requires: R04.
- Defensive evidence: context assertions and diagnostic-kernel failures.
- Current source: missing as an explicit lesson; fragments exist across concurrency,
  instrumentation, and networking scaffolds.

#### R08 - Defer timer work into process context

- Teaches: minimal timer callbacks, workqueues, publish-before-queue, synchronous
  cancel, and flush ordering.
- Produces: a periodic sleep-capable scheduler.
- Requires: R06 and R07.
- Defensive evidence: measured ticks, no work after unload, and unload while work is
  pending.
- Current sources: L17 and L18.

#### R09 - Run and stop an event-driven kernel thread

- Teaches: thread creation and pointer errors, wait queues, condition-loop waiting,
  publish-before-wake, stop checks, wakeup, and join.
- Produces: the resident worker.
- Requires: R06 and R07.
- Defensive evidence: task inventory, event consumption, stop while blocked, and
  disappearance after unload.
- Current sources: L19 and L23; L12's real pointer-error application is reinforced
  here.

#### M00 - Controlled resident core

Merge the useful integration work of C00 and C01.
The shared control switch gates the scheduler and resident worker.
Periodic work updates status; the worker accepts a fixed wake event; both actions stop when disabled and unload cleanly.
Requires R08 and R09 directly.
This extends the cumulative R04 project, not a fresh scaffold.

### Stage 3: Identity, authorization and observation

#### R10 - Attribute an operation to the current task

- Teaches: `current`, stable snapshots of PID and command name, and why the process
  executing a callback is the caller.
- Produces: attributable control and event records.
- Requires: R05.
- Defensive evidence: independent comparison with the actual reader or writer.
- Current source: L24.

#### R11 - Interpret Linux credentials correctly

- Teaches: real, effective, saved, and filesystem IDs; capabilities; namespace-aware
  ID translation; and read-only credential access.
- Produces: an authorization-context snapshot.
- Requires: R10.
- Defensive evidence: reads under distinct users and namespaces.
- Current source: L25.

#### R12 - Replace credentials transactionally

- Teaches: prepare, populate, commit, abort, and same-caller effect.
- Produces: one bounded privilege action.
- Requires: R06 and R11.
- Defensive evidence: wrong command has no effect; the exact writing process changes;
  failed preparation leaves credentials unchanged.
- Current source: L26.

#### R13 - Authorize a one-use action with fresh state

- Teaches: kernel randomness, unpredictability, authorization versus authentication,
  rotation, erasure limits, replay rejection, bounded typed parsing, and a mutex
  protecting the multi-field one-use invariant.
- Produces: a one-use challenge protecting R12.
- Requires: R12.
- Defensive evidence: malformed, stale, reused, and current values are distinguished.
- Current sources: L28 and the state machine in C03.

#### R14 - Resolve symbols under a pinned ABI contract

- Teaches: exported versus internal symbols, configuration gates, architecture call
  ABI, kernel-version coupling, and the narrow temporary-probe resolver.
- Produces: an explicit compatibility boundary for later instrumentation.
- Requires: R07.
- Defensive evidence: resolved address, configuration/version manifest, and absence
  of the temporary probe afterward.
- Current source: L27, moved from general introspection to immediately before probes.

#### R15 - Observe function entry with a managed kprobe

- Teaches: registration, bounded handlers, per-hook ownership, unregister-before-free,
  and rollback of a partially registered hook set.
- Produces: entry telemetry.
- Requires: R14.
- Defensive evidence: probe registry, event counter, forced second-hook failure, and
  complete removal.
- Current sources: L29 and L33.

#### R16 - Correlate function return and decode syscall arguments

- Teaches: kretprobe entry/return state, `maxactive` and missed instances, syscall
  wrapper registers, no-fault bounded copying, and architecture limitations.
- Produces: attributable syscall telemetry.
- Requires: R06 and R15.
- Defensive evidence: success/failure return classes, exact matching argument,
  ordinary calls unchanged, and missed-instance reporting.
- Current sources: L30 and L32.

#### M01 - Authorized actions and telemetry

Merge C03 and C04.
Add the one-use same-caller privilege action and a bounded event ring for selected file activity.
Spinlock ownership and ring overwrite semantics must be taught before or within a tightly scoped guided part of this milestone; they must not appear as unexplained scaffold magic.

### Stage 4: Views, concealment and detection

#### R17 - Distinguish an object from a rendered view

- Teaches: underlying object, enumeration path, direct lookup, positive and negative
  controls, cross-view comparison, and independent evidence.
- Produces: the evidence model used by every concealment mechanism.
- Requires: R05 and R15.
- Defensive evidence: a view can disagree while the object continues to exist.
- Current source: currently implicit across L34-L39; make it explicit.

#### R18 - Alter one module view without claiming invisibility

- Teaches: module-list membership, sysfs/kobject artifacts, reversible unlink/relink,
  intrusive kernel lists, and exact restoration.
- Produces: controlled module enumeration suppression.
- Requires: R17.
- Defensive evidence: module inventory disagrees with sysfs and other artifacts.
- Current sources: L34 and the useful evidence/lifecycle part of L35.

#### R19 - Filter packed directory records safely

- Teaches: validation of minimum and full record extent, name termination, forward
  progress, overlap-safe compaction, and returned-length correction.
- Produces: reusable directory-enumeration filtering.
- Requires: R16 and R17.
- Defensive evidence: direct lookup, unrelated directories, malformed records, and
  restoration after unload.
- Current source: L36.

#### R20 - Filter a bounded rendered result

- Teaches: one general read-result/row-filtering invariant and why each reporting
  path is only one view.
- Produces: a reusable bounded content filter.
- Requires: R16 and R17.
- Defensive evidence: underlying bytes or object remain independently visible.
- Current source: L37 as the required implementation; L38 and L39 become comparative
  optional labs.

#### M02 - Concealment with an evidence map

Refactor C05 into the cumulative project.
Hide the resident worker PID from proc root enumeration while preserving direct lookup, task evidence, scheduling, and control.
The milestone produces a machine-readable evidence card stating which view changed and which views did not.

### Stage 5: Networking and durable state

#### R21 - Parse hostile packet lengths before touching payload

- Teaches: network byte order, IPv4 IHL and total length, fragment policy, transport
  boundaries, overflow-safe arithmetic, and truncation.
- Produces: host-tested protocol parsers and fixtures.
- Requires: R07.
- Defensive evidence: malformed and truncated fixture corpus.
- Current sources: concepts are spread across L41, L42, L44, and L46 scaffolds.

#### R22 - Read an `sk_buff` without assuming linear storage

- Teaches: data/head/tail/end geometry, header offsets, nonlinear/shared storage,
  safe header access, and bounded copy helpers.
- Produces: a safe packet-view adapter.
- Requires: R21.
- Defensive evidence: linear, nonlinear, truncated, and fragmented cases.
- Current source: missing explicitly; current packet lessons hide part of this work.

#### R23 - Register a non-disruptive Netfilter observer

- Teaches: hook points, namespaces, priorities, verdicts, registration lifetime, and
  accept-by-default behavior.
- Produces: a packet observation point.
- Requires: R22.
- Defensive evidence: hook inventory/counters, unchanged traffic, and removal.
- Current source: L40.

#### R24 - Classify only fully validated packets

- Teaches: layered protocol predicates, exact matching, fragment policy, and
  malformed-input state preservation.
- Produces: bounded IPv4 and ICMP/transport classification.
- Requires: R23.
- Defensive evidence: matching, nonmatching, truncated, fragmented, and unrelated
  traffic.
- Current sources: L41 and the predicate work of L42.

#### R25 - Mutate a writable packet and repair its checksum state

- Teaches: copy-on-write, reacquiring pointers, length consistency, checksum and
  offload metadata, and narrow scope.
- Produces: one controlled packet redirection.
- Requires: R24.
- Defensive evidence: exact target is redirected; unrelated traffic is unchanged.
- Current source: L44.

#### R26 - Own a kernel socket from creation through release

- Teaches: namespace-aware creation, connect/send/receive errors, datagram exactness,
  shutdown, release, and visibility in socket tables.
- Produces: a kernel-owned endpoint.
- Requires: R07.
- Defensive evidence: socket inventory and disappearance after failure or unload.
- Current source: L43, refactored so the title and implementation agree on the
  actual socket family and type.

#### R27 - Frame and cancel a TCP client correctly

- Teaches: byte-stream framing, partial I/O, oversize-frame draining, retry/backoff,
  blocked-I/O cancellation, and one socket owner.
- Produces: the fixed-function resident agent transport.
- Requires: R09 and R26.
- Defensive evidence: split/coalesced frames, reconnects, malformed commands,
  unload while blocked, and socket cleanup.
- Current source: the transport work in C07.

#### R28 - Encode and decode a bounded DNS wire name

- Teaches: length-prefixed labels, terminal root, exact 63/255 bounds, compression
  policy, and failure without prior-output mutation.
- Produces: a DNS-shaped message component.
- Requires: R21.
- Defensive evidence: boundary vectors and malformed names.
- Current source: L47.

#### R29 - Apply and conditionally roll back a file transaction

- Teaches: exact original image, bounded kernel file I/O, foreign-file ownership,
  compare-before-restore, and context/deadlock constraints.
- Produces: a guarded durable boot-request edit.
- Requires: R07.
- Defensive evidence: byte-for-byte restoration, sentinel protection, preservation
  of newer foreign content, and file-integrity monitoring.
- Current source: L48.

#### M03 - Remote resident agent

Merge C07 and C08 into the cumulative project.
The module connects only inside the declared lab network, accepts a small fixed command set such as `PING`, `INFO`, and a harmless bounded marker, exposes retry/error counters, unloads while blocked, and manages only its exact boot request.
This milestone does not expose a general shell.

### Stage 6: Authenticated operations

#### R30 - Choose the protection property before the primitive

- Teaches: encoding, XOR obfuscation, confidentiality, integrity, authentication,
  freshness, replay, and traffic-analysis limits.
- Produces: a threat-to-primitive decision table.
- Requires: R28.
- Defensive evidence: plaintext recovery and static/runtime artifact comparison.
- Current source: L49 becomes one demonstration inside this lesson, not a claim of
  cryptographic protection.

#### R31 - Compute and verify SHA-256 correctly

- Teaches: Kernel Crypto API `shash` lifecycle, descriptor sizing, known-answer
  vectors, comparison, and why a public digest is not authentication.
- Produces: integrity verification against a trusted fingerprint.
- Requires: R30.
- Defensive evidence: known-answer accept/reject cases and keyless forgery model.
- Current source: L51.

#### R32 - Authenticate a message and reject replay

- Teaches: HMAC, key ownership, tag truncation budget, constant-time comparison,
  sequence/challenge state, and fail-closed updates.
- Produces: authenticated fixed commands.
- Requires: R13 and R31.
- Defensive evidence: known-answer tags, tampering, wrong key, stale sequence,
  duplicate sequence, and state unchanged on failure.
- Current source: L52 plus missing replay policy.

#### R33 - Use AEAD without releasing unauthenticated plaintext

- Teaches: transform/request/scatterlist lifetime, AAD, unique nonces, tag layout,
  asynchronous completion, `-EBADMSG`, and secret-erasure limits.
- Produces: an optional confidential transport envelope.
- Requires: R32.
- Defensive evidence: known-answer vector, nonce discipline, tag/ciphertext tamper,
  and no plaintext consumption on failure.
- Current source: L50, moved after the simpler hash and MAC property progression.

#### R34 - Emit an authenticated DNS-shaped signal

- Teaches: composition of bounded DNS encoding, HMAC/freshness, socket ownership,
  and a paired detection hypothesis.
- Produces: one local authenticated status signal, not a general command tunnel.
- Requires: R17, R26, and R32.
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
  R30, R33, and R34.

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
| L12 | Merge | R03/R09 | Pointer errors are taught where allocation and kthread creation return them. |
| L13 | Merge | R18 | Only the intrusive-list mechanics survive, where module-list manipulation consumes them. |
| L14 | Merge | E02 | Device registration alone is an incomplete optional ABI. |
| L15 | Merge | E02 | Valuable alternate typed control plane, not required for the proc-based core. |
| L16 | Move/optional | E03 | Unique boundary; current proc prerequisite is false for its task. |
| L17 | Merge | R08 | Timer exists to motivate context deferral. |
| L18 | Merge | R08 | Workqueue completes the timer/context mechanism. |
| L19 | Merge | R09 | Resident thread plus wait/stop lifecycle is the meaningful unit. |
| L20 | Merge | R06 | Atomic scalar state implements the action switch. |
| L21 | Optional | E04 | Completion is useful but has no required downstream consumer. |
| L22 | Merge | R13 | Mutex protection is taught where a real multi-field one-use invariant needs it. |
| L23 | Merge | R09 | Wait queue is part of the event-driven worker lifecycle. |
| C00 | Merge | R06/M00 | Its bounded fixed control is taught in R06 and integrated with resident actions in M00. |
| C01 | Merge | M00 | Scheduler belongs to the same controlled resident core. |
| C02 | Optional | E04 | Strong concurrency synthesis, but not consumed by the required route. |
| L24 | Refactor | R10 | Caller attribution remains explicit before credentials. |
| L25 | Refactor | R11 | Credential vocabulary and namespaces require their own contract. |
| L26 | Refactor | R12 | Transactional same-caller credential change is core. |
| L27 | Move/refactor | R14 | Belongs immediately before instrumentation and must state version/ABI fragility. |
| L28 | Merge | R13 | Randomness is taught through the one-use authorization state that needs it. |
| C03 | Merge | M01 | Integrates privilege and one-use authorization into the cumulative rootkit. |
| L29 | Merge | R15 | Entry probe and hook-set rollback form one managed lifecycle. |
| L30 | Merge | R16 | Return correlation remains required for later view filters. |
| L31 | Optional | E05 | Fprobe is an alternate instrumentation API, not a new capability. |
| L32 | Merge | R16 | Syscall ABI and no-fault argument copying complete return-aware probing. |
| L33 | Merge | R15 | Partial hook rollback is essential but too thin as a separate task. |
| C04 | Merge | M01 | Telemetry joins the authorized-action milestone. |
| L34 | Refactor | R18 | Precise one-view suppression with independent evidence. |
| L35 | Merge | R18 | Sysfs artifacts provide the necessary cross-view and lifecycle evidence. |
| L36 | Refactor | R19 | Central variable-record filter reused by process concealment. |
| L37 | Refactor | R20 | Representative required rendered-result filter. |
| L38 | Optional | E06 | Comparative read-view variant with no core consumer. |
| L39 | Optional | E06 | Comparative seq-table variant with no core consumer. |
| C05 | Refactor | M02 | Strong cumulative concealment and cross-view milestone. |
| C06 | Optional | E06 | Duplicates control storage, filtering, and trigger mechanisms. |
| L40 | Refactor | R23 | Netfilter lifecycle remains explicit after skb preparation. |
| L41 | Merge | R24 | Classification is the useful result of packet observation. |
| L42 | Merge/optional | R24/E07 | Predicate is core parsing; ICMP channel variant is optional. |
| L43 | Refactor/split | R26/E07 | Generic socket ownership is core; raw ICMP specificity is optional. |
| L44 | Refactor | R25 | Distinct active mutation and checksum invariant. |
| L45 | Optional | E07 | Fixed-function agent is safer and pedagogically richer than a required shell. |
| L46 | Optional | E07 | Alternate ICMP carrier after required validated parsing. |
| L47 | Refactor | R28 | Strong bounded protocol encoder used by authenticated signaling. |
| C07 | Refactor | R27/M03 | Transport mechanics become a lesson; integration extends the learner's project. |
| L48 | Refactor | R29 | Guarded reversible file transaction remains core. |
| C08 | Merge | M03 | Boot request joins the remote resident milestone; do not claim a reboot test that does not run. |
| L49 | Merge/optional | R30/E08 | XOR is an honest obfuscation demonstration, never encryption. |
| L50 | Move/refactor | R33/E08 | AEAD follows hash and HMAC property progression. |
| L51 | Refactor | R31 | Public digest precedes keyed authentication. |
| L52 | Refactor | R32 | Add replay state and fail-closed update semantics. |
| L53 | Merge/optional | R34/E08 | Keyed time-bucket label is applied to the authenticated signal and defender analysis. |
| C09 | Refactor | R34 | Required bounded authenticated signal; no general DNS command tunnel. |
| C10 | Refactor | C00 | Only final capstone; reuse the learner's cumulative implementation. |

## Concepts currently missing or under-taught

These gaps explain why migrating the current 65 items directly into 34 mechanism lessons would be incomplete.
They must be added, made explicit, or consciously excluded.

- Execution-context and sleepability matrix.
- Spinlocks and atomic-context synchronization before the event ring.
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
5. Build R01-R09 and M00 first; validate the complete novice-to-resident-core
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
