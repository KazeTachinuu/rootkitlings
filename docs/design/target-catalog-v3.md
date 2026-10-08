# Target catalog v3

Status: approved design input; not yet the public route.

This document translates the curriculum migration map into an exact target catalog.
Stable IDs are semantic; aliases are presentation-only.
Direct prerequisites deliberately omit transitive duplicates.

Stage keys: `S1=lab-core`, `S2=control-execution`, `S3=identity-observation`, `S4=views-detection`, `S5=network-state`, `S6=authenticated-operations`, and `E=electives`.

Asset status: `adapt` means current material is useful but does not satisfy the new contract; `new` means no adequate target exercise exists; `reuse` means the existing contract is substantially sufficient.

## Time estimates

The catalog does not store `estimated_minutes`.
A precise duration without observed learner data would be invented metadata, and VM speed alone can materially change check time.
Use the defined difficulty level for planning until real cohorts provide enough samples to publish a median and range by experience level and execution environment.
Verification timeouts remain machine limits, not claims about learning time.

## Required lessons

| Alias | Stable ID / title | Stage | Direct prerequisites | Teaches | Requires | Tradecraft | Current sources | Assets |
|---|---|---|---|---|---|---|---|---|
| L00 | `authorized-kernel-lab` — Work inside an authorized disposable lab | S1 | — | `authorized-lab-boundary`, `guest-reset-cleanup` | baseline assumptions | lifecycle, detection | none | new |
| L01 | `module-build-lifecycle` — Build, load, inspect, and unload a module | S1 | `authorized-kernel-lab` | `module-lifecycle`, `module-build`, `module-inspection` | `authorized-lab-boundary` | lifecycle, detection | L00, L01 | adapt |
| L02 | `component-ownership` — Split a module into owned components | S1 | `module-build-lifecycle` | `multi-file-modules`, `component-ownership` | `module-build` | lifecycle | L02 | adapt |
| L03 | `precise-init-failure` — Reject initialization with a precise error | S1 | `module-build-lifecycle` | `init-failure`, `errno-propagation` | `module-lifecycle` | lifecycle, detection | L04 | adapt |
| L04 | `resource-rollback` — Own resources and roll back in reverse order | S1 | `component-ownership`, `precise-init-failure` | `initialization-rollback`, `resource-ownership` | `component-ownership`, `init-failure` | lifecycle, detection | L08 | adapt |
| L05 | `owned-kernel-state` — Own heap state and pointer-shaped failures | S1 | `resource-rollback` | `kernel-allocation`, `error-pointers`, `pointer-invalidation` | `initialization-rollback` | lifecycle | L07, L12 | adapt |
| L06 | `proc-publication` — Publish and remove a procfs status endpoint | S2 | `resource-rollback` | `proc-entry`, `proc-publication-lifetime` | `initialization-rollback` | control, detection | L09 | adapt |
| L07 | `proc-read-contract` — Implement correct procfs reads | S2 | `proc-publication` | `proc-read`, `bounded-read-contract` | `proc-entry` | control, detection | L10 | adapt |
| L08 | `bounded-write-transaction` — Accept a bounded userspace write transaction | S2 | `precise-init-failure`, `proc-publication` | `proc-write`, `bounded-user-copy`, `copy-validate-commit` | `init-failure`, `proc-entry` | control | L11 | adapt |
| L09 | `fixed-command-grammar` — Parse a fixed command grammar | S2 | `component-ownership`, `bounded-write-transaction` | `fixed-command-grammar`, `bounded-typed-parsing` | `component-ownership`, `copy-validate-commit` | control | C00 and later projects | new |
| L10 | `owned-configuration-store` — Store owned configuration records | S2 | `owned-kernel-state`, `bounded-write-transaction` | `kernel-lists`, `configuration-store` | `kernel-allocation`, `copy-validate-commit` | control, lifecycle | L13 | adapt |
| L11 | `state-invariant-selection` — Protect the correct state invariant | S2 | `bounded-write-transaction`, `owned-configuration-store` | `atomic-operations`, `mutexes`, `mutex-protected-invariants`, `state-invariant-selection` | `copy-validate-commit`, `configuration-store` | control | L20, L22 | adapt |
| L12 | `execution-context` — Choose a legal execution context | S2 | `resource-rollback` | `execution-context`, `sleepability`, `atomic-context-rules` | `resource-ownership` | lifecycle, execution | fragments only | new |
| L13 | `timer-workqueue-scheduler` — Defer timer work into process context | S2 | `state-invariant-selection`, `execution-context` | `kernel-timers`, `workqueues`, `deferred-work-lifecycle` | `state-invariant-selection`, `execution-context` | execution, lifecycle | L17, L18 | adapt |
| L14 | `event-driven-kernel-thread` — Run and stop an event-driven kernel thread | S2 | `owned-kernel-state`, `state-invariant-selection`, `execution-context` | `kernel-threads`, `wait-queues`, `worker-stop-lifecycle` | `error-pointers`, `state-invariant-selection`, `execution-context` | execution, lifecycle | L19, L23, part of L12 | adapt |
| L15 | `current-task-attribution` — Attribute an operation to the current task | S3 | `proc-read-contract` | `current-task`, `task-attribution` | `proc-read` | instrumentation, detection | L24 | adapt |
| L16 | `linux-credential-model` — Interpret Linux credentials correctly | S3 | `current-task-attribution` | `task-credentials`, `namespace-id-translation` | `current-task` | privilege, detection | L25 | adapt |
| L17 | `transactional-credential-replacement` — Replace credentials transactionally | S3 | `owned-kernel-state`, `bounded-write-transaction`, `linux-credential-model` | `credential-replacement` | `kernel-allocation`, `copy-validate-commit`, `task-credentials` | privilege | L26 | adapt |
| L18 | `one-use-authorization` — Authorize a one-use action with fresh state | S3 | `state-invariant-selection`, `transactional-credential-replacement` | `kernel-randomness`, `one-use-authorization`, `replay-rejection` | `mutex-protected-invariants`, `credential-replacement` | privilege, control, cryptography | L28, C03 state machine | adapt |
| L19 | `pinned-symbol-resolution` — Resolve symbols under a pinned ABI contract | S3 | `component-ownership`, `execution-context` | `symbol-resolution`, `kernel-abi-contract` | `component-ownership`, `execution-context` | instrumentation, lifecycle | L27 | adapt |
| L20 | `managed-kprobe` — Observe function entry with a managed kprobe | S3 | `resource-rollback`, `pinned-symbol-resolution` | `kprobes`, `hook-rollback`, `managed-hook-lifecycle` | `resource-ownership`, `symbol-resolution` | instrumentation, detection | L29, L33 | adapt |
| L21 | `return-aware-syscall-telemetry` — Correlate function return and decode syscall arguments | S3 | `bounded-write-transaction`, `managed-kprobe` | `kretprobes`, `syscall-argument-decoding`, `return-correlation` | `bounded-user-copy`, `managed-hook-lifecycle` | instrumentation, detection | L30, L32 | adapt |
| L22 | `cross-view-evidence` — Distinguish an object from a rendered view | S4 | `proc-read-contract`, `managed-kprobe` | `cross-view-evidence`, `positive-negative-controls` | `proc-read`, `kprobes` | detection, concealment | implicit in L34–L39 | new |
| L23 | `module-view-suppression` — Alter one module view without claiming invisibility | S4 | `owned-configuration-store`, `cross-view-evidence` | `module-list-hiding`, `sysfs-artifacts`, `reversible-view-suppression` | `kernel-lists`, `cross-view-evidence` | concealment, detection | L34, L35 | adapt |
| L24 | `packed-directory-filter` — Filter packed directory records safely | S4 | `owned-kernel-state`, `return-aware-syscall-telemetry`, `cross-view-evidence` | `directory-filtering`, `packed-record-validation` | `kernel-allocation`, `syscall-argument-decoding`, `cross-view-evidence` | concealment, detection | L36 | adapt |
| L25 | `bounded-rendered-filter` — Filter a bounded rendered result | S4 | `return-aware-syscall-telemetry`, `cross-view-evidence` | `read-view-filtering`, `bounded-rendered-filter` | `kretprobes`, `cross-view-evidence` | concealment, detection | L37 | adapt |
| L26 | `hostile-packet-lengths` — Parse hostile packet lengths before touching payload | S5 | `execution-context` | `packet-length-validation`, `network-byte-order-policy` | `execution-context`, baseline networking | networking, detection | fragments in L41/L42/L44/L46 | new |
| L27 | `safe-skb-access` — Read an sk_buff without assuming linear storage | S5 | `hostile-packet-lengths` | `skb-safe-access`, `nonlinear-packet-access` | `packet-length-validation` | networking | hidden in current scaffolds | new |
| L28 | `netfilter-observer` — Register a non-disruptive Netfilter observer | S5 | `resource-rollback`, `execution-context`, `safe-skb-access` | `netfilter-hooks`, `accept-by-default-observation` | `resource-ownership`, `execution-context`, `skb-safe-access` | networking, instrumentation, detection | L40 | adapt |
| L29 | `validated-packet-classification` — Classify only fully validated packets | S5 | `netfilter-observer` | `packet-classification`, `malformed-input-state-preservation` | `netfilter-hooks`, `skb-safe-access` | networking, detection | L41, predicate portion of L42 | adapt |
| L30 | `writable-packet-mutation` — Mutate a writable packet and repair checksum state | S5 | `validated-packet-classification` | `packet-redirection`, `packet-copy-on-write`, `checksum-repair` | `packet-classification` | networking | L44 | adapt |
| L31 | `kernel-socket-ownership` — Own a kernel socket from creation through release | S5 | `resource-rollback`, `execution-context` | `kernel-sockets`, `socket-lifecycle` | `resource-ownership`, `execution-context` | networking, lifecycle, detection | L43 | adapt |
| L32 | `tcp-framing-cancellation` — Frame and cancel a TCP client correctly | S5 | `event-driven-kernel-thread`, `kernel-socket-ownership` | `tcp-framing`, `blocked-io-cancellation`, `connect-back-agent` | `worker-stop-lifecycle`, `socket-lifecycle` | networking, control, execution | transport portion of C07 | adapt |
| L33 | `bounded-dns-wire-name` — Encode and decode a bounded DNS wire name | S5 | `hostile-packet-lengths` | `dns-wire-encoding` | `packet-length-validation`, baseline networking | networking | L47 | adapt |
| L34 | `guarded-file-transaction` — Apply and conditionally roll back a file transaction | S5 | `owned-kernel-state`, `resource-rollback`, `execution-context` | `kernel-file-transactions`, `foreign-file-ownership` | `kernel-allocation`, `resource-ownership`, `execution-context` | persistence, lifecycle, detection | L48 | adapt |
| L35 | `protection-property-selection` — Choose the protection property before the primitive | S6 | `bounded-dns-wire-name` | `protection-property-selection`, `xor-obfuscation`, `traffic-analysis-limits` | `dns-wire-encoding` | cryptography, detection | L49 demonstration | adapt |
| L36 | `sha256-verification` — Compute and verify SHA-256 correctly | S6 | `owned-kernel-state`, `protection-property-selection` | `kernel-crypto-api`, `sha256-verification` | `kernel-allocation`, `protection-property-selection` | cryptography, detection | L51 | adapt |
| L37 | `authenticated-replay-safe-message` — Authenticate a message and reject replay | S6 | `one-use-authorization`, `sha256-verification` | `hmac-authentication`, `replay-resistant-state` | `replay-rejection`, `kernel-crypto-api`, `sha256-verification` | cryptography, control, detection | L52 plus missing replay policy | adapt |
| L38 | `authenticated-encryption` — Use AEAD without releasing unauthenticated plaintext | S6 | `owned-kernel-state`, `authenticated-replay-safe-message` | `authenticated-encryption`, `aead-plaintext-gating` | `kernel-allocation`, `hmac-authentication`, `replay-resistant-state` | cryptography | L50 | adapt |
| L39 | `authenticated-dns-signal` — Emit an authenticated DNS-shaped signal | S6 | `cross-view-evidence`, `kernel-socket-ownership`, `bounded-dns-wire-name`, `authenticated-replay-safe-message` | `authenticated-dns-signals`, `paired-network-detection` | `cross-view-evidence`, `socket-lifecycle`, `dns-wire-encoding`, `hmac-authentication`, `replay-resistant-state` | networking, cryptography, detection | C09, bounded-label idea from L53 | adapt |

## Required milestones and capstone

| Alias | Stable ID / title | Stage | Direct prerequisites | Teaches | Requires | Tradecraft | Sources | Assets |
|---|---|---|---|---|---|---|---|---|
| M00 | `inspectable-core` — Inspectable core | S1 | `owned-kernel-state` | `inspectable-core-integration` | `component-ownership`, `resource-ownership`, `kernel-allocation`, `error-pointers` | lifecycle, detection | L00–L02, L04, L07, L08, L12 | new |
| M01 | `configurable-resident-core` — Configurable resident core | S2 | `inspectable-core`, `fixed-command-grammar`, `owned-configuration-store`, `timer-workqueue-scheduler`, `event-driven-kernel-thread` | `resident-core-integration` | `fixed-command-grammar`, `configuration-store`, `deferred-work-lifecycle`, `worker-stop-lifecycle` | control, execution, lifecycle | C00, C01 | new |
| M02 | `authorized-actions-telemetry` — Authorized actions and telemetry | S3 | `configurable-resident-core`, `one-use-authorization`, `return-aware-syscall-telemetry` | `spinlocked-event-ring`, `authorized-telemetry-integration` | `resident-core-integration`, `one-use-authorization`, `return-correlation` | privilege, instrumentation, detection | C03, C04 | new |
| M03 | `concealment-evidence-map` — Concealment with an evidence map | S4 | `authorized-actions-telemetry`, `packed-directory-filter`, `cross-view-evidence` | `ghost-process-view`, `machine-readable-evidence-map` | `authorized-telemetry-integration`, `directory-filtering`, `cross-view-evidence` | concealment, detection | C05 | new |
| M04 | `remote-resident-agent` — Remote resident agent | S5 | `concealment-evidence-map`, `tcp-framing-cancellation`, `guarded-file-transaction` | `remote-agent-integration`, `boot-load-persistence` | `machine-readable-evidence-map`, `connect-back-agent`, `kernel-file-transactions` | control, networking, persistence, lifecycle | C07, C08 | new |
| C00 | `operation-glasshouse` — Operation Glasshouse: validate a rootkit through independent views | S6 | `remote-resident-agent`, `authenticated-dns-signal` | `adversarial-validation` | `remote-agent-integration`, `authenticated-dns-signals`, `machine-readable-evidence-map` | lifecycle, control, privilege, instrumentation, concealment, networking, persistence, cryptography, detection | C10 plus C03/C05/C08/C09 behavior | new |

Milestone workspace parents are `M00 → M01 → M02 → M03 → M04 → C00`.
Workspace inheritance is separate from knowledge prerequisites.

## Elective tracks

Elective tracks belong in a separate course-level registry with generated E-series labels.
Their member exercises remain ordinary elective lessons; a track is a named, ordered bundle rather than a runnable exercise kind.

| Alias | Stable ID / title | Kind/status | Direct prerequisites | Teaches | Requires | Tradecraft | Sources | Assets |
|---|---|---|---|---|---|---|---|---|
| E01 | `kernel-debugging-track` — Kernel debugging | track/elective | `module-build-lifecycle` | `kernel-debugging` | `module-build` | instrumentation, detection | L03 | reuse/adapt only for new naming |
| E02 | `binary-control-abi-track` — Alternate binary control ABI | track/elective | `bounded-write-transaction` | `character-devices`, `ioctl-dispatch`, `versioned-uapi`, `compat-ioctl` | `bounded-user-copy` | control | L14, L15 | adapt; versioned/compat contract is new |
| E03 | `usermode-helper-boundary-track` — Fixed userspace-helper boundary | track/elective | `resource-rollback` | `usermode-helper` | `resource-ownership` | execution, detection | L16 | adapt; remove false proc-write dependency |
| E04 | `advanced-concurrency-diagnostics-track` — Advanced concurrency and diagnostics | track/elective | `state-invariant-selection`, `event-driven-kernel-thread` | `completions`, `dead-man-watchdog`, `lock-ordering`, `memory-ordering`, `reference-counting`, `kernel-concurrency-diagnostics` | `state-invariant-selection`, `worker-stop-lifecycle` | lifecycle, execution, detection | L21, C02, portions L20/L23 | new beyond partial sources |
| E05 | `alternate-instrumentation-track` — Alternate instrumentation | track/elective | `managed-kprobe` | `fprobes`, `ftrace`, `tracepoints`, `defensive-bpf-comparison` | `managed-hook-lifecycle` | instrumentation, detection | L31 | new beyond fprobe material |
| E06 | `alternate-views-storage-track` — Alternate views and hidden storage | track/elective | `packed-directory-filter`, `bounded-rendered-filter` | `log-view-filtering`, `socket-row-filtering`, `sealed-dead-drop` | `directory-filtering`, `bounded-rendered-filter`, `cross-view-evidence` | concealment, detection | L38, L39, C06 | adapt |
| E07 | `alternate-packet-channels-track` — Alternate packet channels and helper handoff | track/elective | `safe-skb-access`, `kernel-socket-ownership` | `icmp-triggers`, `icmp-covert-messaging`, `raw-icmp-sockets`, `controlled-descriptor-handoff`, `tcp-shell-analysis` | `skb-safe-access`, `socket-lifecycle` | networking, control, detection | L42, raw portion L43, L45, L46 | adapt |
| E08 | `crypto-rendezvous-extensions-track` — Cryptographic and rendezvous extensions | track/elective | `authenticated-encryption`, `authenticated-dns-signal` | `xor-artifact-analysis`, `keyed-domain-generation`, `nxdomain-detection`, `sinkhole-analysis` | `protection-property-selection`, `authenticated-encryption`, `authenticated-dns-signals` | cryptography, networking, detection | L49, L50, L53 | adapt |

## Concept-registry changes

Add these genuinely new concepts:

- `authorized-lab-boundary`
- `guest-reset-cleanup`
- `module-inspection`
- `component-ownership`
- `errno-propagation`
- `resource-ownership`
- `pointer-invalidation`
- `proc-publication-lifetime`
- `bounded-read-contract`
- `copy-validate-commit`
- `fixed-command-grammar`
- `bounded-typed-parsing`
- `state-invariant-selection`
- `execution-context`
- `sleepability`
- `atomic-context-rules`
- `deferred-work-lifecycle`
- `worker-stop-lifecycle`
- `task-attribution`
- `namespace-id-translation`
- `one-use-authorization`
- `replay-rejection`
- `kernel-abi-contract`
- `managed-hook-lifecycle`
- `return-correlation`
- `cross-view-evidence`
- `positive-negative-controls`
- `reversible-view-suppression`
- `packed-record-validation`
- `bounded-rendered-filter`
- `packet-length-validation`
- `network-byte-order-policy`
- `skb-safe-access`
- `nonlinear-packet-access`
- `accept-by-default-observation`
- `malformed-input-state-preservation`
- `packet-copy-on-write`
- `checksum-repair`
- `socket-lifecycle`
- `tcp-framing`
- `blocked-io-cancellation`
- `foreign-file-ownership`
- `protection-property-selection`
- `traffic-analysis-limits`
- `replay-resistant-state`
- `aead-plaintext-gating`
- `paired-network-detection`
- `inspectable-core-integration`
- `resident-core-integration`
- `spinlocked-event-ring`
- `authorized-telemetry-integration`
- `machine-readable-evidence-map`
- `remote-agent-integration`
- `versioned-uapi`
- `compat-ioctl`
- `lock-ordering`
- `memory-ordering`
- `reference-counting`
- `kernel-concurrency-diagnostics`
- `ftrace`
- `tracepoints`
- `defensive-bpf-comparison`
- `raw-icmp-sockets`
- `controlled-descriptor-handoff`
- `tcp-shell-analysis`
- `xor-artifact-analysis`
- `nxdomain-detection`
- `sinkhole-analysis`

Deprecate or reclassify these current product/API-shaped concepts:

- `multi-file-modules` remains a mechanism but is paired with
  `component-ownership`.
- `one-time-root-trigger` becomes `one-use-authorization`.
- `hook-rollback` remains, paired with `managed-hook-lifecycle`.
- `ghost-process-view` becomes a milestone output rather than a standalone
  required mechanism.
- `connect-back-agent` becomes a transport output under
  `tcp-framing-cancellation`.
- `xor-obfuscation` becomes a demonstration within
  `protection-property-selection`.
- `keyed-domain-generation`, `tcp-shell`, `icmp-triggers`,
  `icmp-covert-messaging`, `fprobes`, `completions`, `dead-man-watchdog`,
  `sealed-dead-drop`, `log-view-filtering`, and `socket-row-filtering` become
  elective-only.

Do not delete an old concept merely because it leaves the required route; retain it while an elective still teaches or requires it.

## Asset reality

No complete target asset set currently exists for the redesigned route.

- L00, L09, L12, L22, L26, and L27 have no real standalone current exercise
  satisfying their contracts.
- Every milestone and the final capstone require new cumulative templates,
  checks, hints, and solutions.
- M02 needs an explicitly taught or tightly guided spinlocked ring; the existing
  C04 scaffold cannot silently supply that knowledge.
- L37 lacks the required replay-safe state machine.
- E02, E04, E05, and E08 substantially exceed their current source exercises.
- Existing source bundles are inputs for adaptation.
  Merged checks must not be
  presented as target checks without rewriting their observable and negative
  contracts.
