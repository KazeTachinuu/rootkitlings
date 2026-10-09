# Target catalog v3

Status: approved design input; not yet the public route.

This document translates the curriculum migration map into an exact target catalog.
Stable IDs are semantic; aliases are presentation-only.
Direct prerequisites deliberately omit transitive duplicates.
`Requires` names only knowledge applied by that checkpoint and not taught there; it never stands for every concept reachable through prerequisite closure.

Stage keys: `S1=module-core`, `S2=control-execution`, `S3=identity-observation`, `S4=views-detection`, `S5=network-state`, `S6=authenticated-operations`, and `E=electives`.

Asset status: `adapt` means current material is useful but does not satisfy the new contract; `new` means no adequate target exercise exists; `reuse` means the existing contract is substantially sufficient.

The scored route contains 31 required mechanism lessons, four cumulative milestones, and one final capstone.
Aliases run from L00 through L30, M00 through M03, and C00.

## Mandatory setup gate

Authorization and disposable-lab containment are mandatory setup conditions, not scored exercises.
Before any workspace can be built or booted, the setup gate records the learner's explicit authorization assertion and verifies the default module-free guest has no host filesystem, host disk, or network interface beyond loopback.
This gate is outside lesson aliases, prerequisites, completion counts, and the concept graph.
It does not claim to prove legal authorization; the learner remains responsible for using only systems they are permitted to test.
Later networking checks may enable only the capability declared by their exercise metadata; that exception does not weaken the default guest boundary.

## Entry knowledge

These are general competencies learned outside the course.
They appear in `prereq` only where first needed, through the same `requires` field as course-taught concepts.

| ID | Required competency | First use |
|---|---|---|
| `c-functions-and-errors` | Read small C functions, control flow, return values, and compiler diagnostics. | L00 |
| `shell-and-paths` | Run commands, navigate paths, and edit files in a POSIX-like shell. | L00 |
| `host-guest-kernel-boundary` | Distinguish the host from the disposable guest and userspace from kernel space. | L00 |
| `c-preprocessor` | Read macro calls, compile-time definitions, conditional compilation, and `#error`. | L01 |
| `c-pointers-and-structs` | Work with pointers, `NULL`, struct declarations, arrays, and function declarations. | L02 |
| `linux-process-basics` | Recognize processes, threads, PIDs, signals, and the current executing task. | L08 |
| `linux-identity-basics` | Recognize users, groups, real/effective IDs, and namespaces as distinct identity contexts. | L09 |
| `ipv4-packet-basics` | Recognize the IPv4 header and distinguish ICMP, TCP, and UDP payloads. | L17 |
| `transport-endpoint-basics` | Recognize addresses, ports, stream sockets, and datagram sockets. | L22 |
| `dns-name-basics` | Read a dot-separated DNS name as a sequence of labels. | L24 |
| `linux-file-permission-basics` | Understand paths, ownership, permission bits, and file descriptors. | L25 |
| `file-descriptor-basics` | Understand open file descriptors and userspace read, write, and ioctl calls. | E02 |

Final metadata must be checked against each completed asset.
An unused general competency is omitted rather than attached to L00 as a course-wide bundle.

## Required lessons

| Alias | Stable ID / title | Stage | Direct prerequisites | Teaches | Requires | Tradecraft | Current sources | Assets |
|---|---|---|---|---|---|---|---|---|
| L00 | `module-build-lifecycle` — Build, load, inspect, and unload a kernel module | S1 | — | `module-lifecycle`, `module-build`, `module-inspection` | `c-functions-and-errors`, `shell-and-paths`, `host-guest-kernel-boundary` | lifecycle, detection | L00, L01 | adapt |
| L01 | `precise-init-failure` — Reject initialization with a precise error | S1 | `module-build-lifecycle` | `init-failure`, `errno-propagation`, `build-time-failure-injection` | `module-lifecycle`, `c-preprocessor` | lifecycle, detection | L04 | adapt |
| L02 | `owned-kernel-state` — Allocate and own private kernel state | S1 | `precise-init-failure` | `multi-file-modules`, `component-ownership`, `kernel-allocation`, `error-pointers`, `pointer-invalidation` | `module-build`, `init-failure`, `errno-propagation`, `build-time-failure-injection`, `c-pointers-and-structs` | lifecycle, detection | L02, L07, L12 | adapt |
| L03 | `proc-publication` — Publish and roll back a procfs entry | S1 | `owned-kernel-state` | `initialization-rollback`, `resource-ownership`, `proc-entry`, `proc-publication-lifetime` | `init-failure`, `errno-propagation`, `component-ownership`, `kernel-allocation`, `error-pointers`, `pointer-invalidation`, `build-time-failure-injection` | lifecycle, control, detection | L08, L09 | adapt |
| L04 | `proc-read-contract` — Read module state through procfs | S2 | `proc-publication` | `proc-read`, `bounded-read-contract` | `proc-entry`, `component-ownership`, `proc-publication-lifetime` | control, detection | L10 | adapt |
| L05 | `bounded-control-switch` — Enable and disable module actions through procfs | S2 | `proc-read-contract` | `proc-write`, `bounded-user-copy`, `copy-validate-commit`, `fixed-command-grammar`, `read-once-write-once`, `state-invariant-selection`, `action-control-switch` | `proc-read`, `bounded-read-contract`, `component-ownership`, `proc-publication-lifetime` | control | L11, C00 | adapt |
| L06 | `timer-workqueue-scheduler` — Defer timer work to a worker | S2 | `bounded-control-switch` | `kernel-timers`, `workqueues`, `timer-to-worker-deferral`, `deferred-work-lifecycle` | `resource-ownership`, `read-once-write-once`, `action-control-switch`, `state-invariant-selection` | execution, lifecycle | L17, L18 | adapt |
| L07 | `event-driven-kernel-thread` — Run and stop an event-driven kernel thread | S2 | `bounded-control-switch` | `kernel-threads`, `wait-queues`, `atomic-operations`, `bounded-event-admission`, `worker-stop-lifecycle` | `error-pointers`, `read-once-write-once`, `action-control-switch`, `state-invariant-selection` | execution, lifecycle | L19, L20, L23, part of L12 | adapt |
| L08 | `current-task-attribution` — Attribute a proc read to the executing task | S3 | `proc-read-contract` | `current-task`, `task-attribution` | `proc-read`, `linux-process-basics` | instrumentation, detection | L24 | adapt |
| L09 | `linux-credential-model` — Interpret the current task's credentials | S3 | `current-task-attribution` | `task-credentials`, `namespace-id-translation`, `namespaced-capabilities` | `current-task`, `linux-identity-basics` | privilege, detection | L25 | adapt |
| L10 | `transactional-credential-replacement` — Replace the writing task's credentials as one transaction | S3 | `bounded-control-switch`, `linux-credential-model` | `credential-replacement` | `kernel-allocation`, `copy-validate-commit`, `task-credentials`, `namespaced-capabilities` | privilege | L26 | adapt |
| L11 | `one-use-authorization` — Authorize one credential change with a fresh token | S3 | `transactional-credential-replacement` | `kernel-randomness`, `one-use-authorization`, `authorization-vs-authentication`, `replay-rejection`, `mutexes`, `mutex-protected-invariants`, `bounded-typed-parsing`, `sensitive-state-zeroization` | `credential-replacement`, `action-control-switch`, `resource-ownership` | privilege, control, cryptography | L28, C03 state machine | adapt |
| L12 | `managed-kprobe` — Manage two named Kprobes | S3 | `bounded-control-switch` | `named-kprobe-resolution`, `kernel-abi-contract`, `kprobes`, `hook-rollback`, `managed-hook-lifecycle` | `resource-ownership`, `proc-publication-lifetime`, `read-once-write-once` | instrumentation, detection, lifecycle | L27, L29, L33 | adapt |
| L13 | `return-aware-syscall-telemetry` — Correlate an rmdir path with its result | S3 | `managed-kprobe` | `kretprobes`, `syscall-argument-decoding`, `nofault-user-copy`, `return-correlation` | `bounded-user-copy`, `action-control-switch`, `kernel-abi-contract`, `managed-hook-lifecycle` | instrumentation, detection | L30, L32 | adapt |
| L14 | `cross-view-evidence` — Omit one live probe from a proc view | S4 | `managed-kprobe` | `cross-view-evidence`, `positive-negative-controls` | `proc-read`, `kprobes` | detection, concealment | implicit in L34–L39 | new |
| L15 | `packed-directory-filter` — Filter packed directory records safely | S4 | `return-aware-syscall-telemetry`, `cross-view-evidence` | `packed-record-validation`, `overlap-safe-record-compaction`, `directory-cookie-preservation`, `false-eof-avoidance` | `kretprobes`, `nofault-user-copy`, `kernel-allocation`, `kernel-abi-contract`, `managed-hook-lifecycle`, `cross-view-evidence` | concealment, detection | L36 | adapt |
| L16 | `bounded-rendered-filter` — Filter a bounded rendered result | S4 | `return-aware-syscall-telemetry`, `cross-view-evidence` | `read-view-filtering`, `bounded-rendered-filter` | `kretprobes`, `cross-view-evidence` | concealment, detection | L37 | adapt |
| L17 | `hostile-packet-lengths` — Parse hostile packet lengths before touching payload | S5 | — | `packet-length-validation`, `network-byte-order-policy` | `ipv4-packet-basics` | networking, detection | fragments in L41/L42/L44/L46 | new |
| L18 | `safe-skb-access` — Read an sk_buff without assuming linear storage | S5 | `hostile-packet-lengths` | `skb-safe-access`, `nonlinear-packet-access` | `packet-length-validation` | networking | hidden in current scaffolds | new |
| L19 | `netfilter-observer` — Register a non-disruptive Netfilter observer | S5 | `safe-skb-access`, `proc-publication` | `netfilter-hooks`, `accept-by-default-observation`, `netfilter-callback-constraints` | `resource-ownership`, `skb-safe-access` | networking, instrumentation, detection | L40 | adapt |
| L20 | `validated-packet-classification` — Classify only fully validated packets | S5 | `netfilter-observer` | `packet-classification`, `malformed-input-state-preservation` | `netfilter-hooks`, `skb-safe-access` | networking, detection | L41, predicate portion of L42 | adapt |
| L21 | `writable-packet-mutation` — Mutate a writable packet and repair checksum state | S5 | `validated-packet-classification` | `packet-redirection`, `packet-copy-on-write`, `checksum-repair` | `packet-classification` | networking | L44 | adapt |
| L22 | `kernel-socket-ownership` — Own a kernel socket from creation through release | S5 | `proc-publication` | `kernel-sockets`, `socket-lifecycle`, `kernel-socket-context-constraints` | `resource-ownership`, `transport-endpoint-basics` | networking, lifecycle, detection | L43 | adapt |
| L23 | `tcp-framing-cancellation` — Frame and cancel a TCP client correctly | S5 | `event-driven-kernel-thread`, `kernel-socket-ownership` | `tcp-framing`, `blocked-io-cancellation`, `connect-back-agent` | `worker-stop-lifecycle`, `socket-lifecycle` | networking, control, execution | transport portion of C07 | adapt |
| L24 | `bounded-dns-wire-name` — Encode and decode a bounded DNS wire name | S5 | `hostile-packet-lengths` | `dns-wire-encoding` | `packet-length-validation`, `dns-name-basics` | networking | L47 | adapt |
| L25 | `guarded-file-transaction` — Apply and conditionally roll back a file transaction | S5 | `proc-publication` | `kernel-file-transactions`, `foreign-file-ownership`, `kernel-file-context-constraints` | `kernel-allocation`, `resource-ownership`, `linux-file-permission-basics` | persistence, lifecycle, detection | L48 | adapt |
| L26 | `protection-property-selection` — Choose the protection property before the primitive | S6 | `bounded-dns-wire-name` | `protection-property-selection`, `xor-obfuscation`, `traffic-analysis-limits` | `dns-wire-encoding` | cryptography, detection | L49 demonstration | adapt |
| L27 | `sha256-verification` — Compute and verify SHA-256 correctly | S6 | `protection-property-selection` | `kernel-crypto-api`, `sha256-verification` | `kernel-allocation`, `protection-property-selection` | cryptography, detection | L51 | adapt |
| L28 | `authenticated-replay-safe-message` — Authenticate a message and reject replay | S6 | `one-use-authorization`, `sha256-verification` | `hmac-authentication`, `replay-resistant-state` | `replay-rejection`, `kernel-crypto-api`, `sha256-verification` | cryptography, control, detection | L52 plus missing replay policy | adapt |
| L29 | `authenticated-encryption` — Use AEAD without releasing unauthenticated plaintext | S6 | `authenticated-replay-safe-message` | `authenticated-encryption`, `aead-plaintext-gating` | `kernel-allocation`, `hmac-authentication`, `replay-resistant-state` | cryptography | L50 | adapt |
| L30 | `authenticated-dns-signal` — Emit an authenticated DNS-shaped signal | S6 | `cross-view-evidence`, `kernel-socket-ownership`, `authenticated-replay-safe-message` | `authenticated-dns-signals`, `paired-network-detection` | `cross-view-evidence`, `socket-lifecycle`, `dns-wire-encoding`, `hmac-authentication`, `replay-resistant-state` | networking, cryptography, detection | C09, bounded-label idea from L53 | adapt |

## Required milestones and capstone

| Alias | Stable ID / title | Stage | Direct prerequisites | Teaches | Requires | Tradecraft | Sources | Assets |
|---|---|---|---|---|---|---|---|---|
| M00 | `controlled-resident-core` — Unify timed and requested work | S2 | `timer-workqueue-scheduler`, `event-driven-kernel-thread` | `resident-core-integration` | `action-control-switch`, `read-once-write-once`, `fixed-command-grammar`, `kernel-timers`, `atomic-operations`, `bounded-event-admission`, `worker-stop-lifecycle` | control, execution, lifecycle | C00, C01 | new |
| M01 | `authorized-actions-telemetry` — Record authorized actions and rmdir results | S3 | `controlled-resident-core`, `one-use-authorization`, `return-aware-syscall-telemetry` | `spinlocked-event-ring`, `authorized-telemetry-integration` | `resident-core-integration`, `one-use-authorization`, `return-correlation` | privilege, instrumentation, detection | C03, C04 | new |
| M02 | `concealment-evidence-map` — Concealment with an evidence map | S4 | `authorized-actions-telemetry`, `packed-directory-filter` | `ghost-process-view`, `machine-readable-evidence-map` | `authorized-telemetry-integration`, `directory-filtering`, `cross-view-evidence` | concealment, detection | C05 | new |
| M03 | `remote-resident-agent` — Remote resident agent | S5 | `concealment-evidence-map`, `tcp-framing-cancellation`, `guarded-file-transaction` | `remote-agent-integration`, `boot-load-persistence` | `machine-readable-evidence-map`, `connect-back-agent`, `kernel-file-transactions` | control, networking, persistence, lifecycle | C07, C08 | new |
| C00 | `operation-glasshouse` — Operation Glasshouse: validate a rootkit through independent views | S6 | `remote-resident-agent`, `authenticated-dns-signal` | `adversarial-validation` | `remote-agent-integration`, `authenticated-dns-signals`, `machine-readable-evidence-map` | lifecycle, control, privilege, instrumentation, concealment, networking, persistence, cryptography, detection | C10 plus C03/C05/C08/C09 behavior | new |

`proc-publication` is the cumulative project root.
Milestone workspace parents are `proc-publication → M00 → M01 → M02 → M03 → C00`.
Workspace inheritance is separate from knowledge prerequisites.
M00 therefore starts from the learner's passed `proc-publication` tree, not from any L05-L07 lesson workspace.
Those lessons practice mechanisms separately; M00 asks the learner to apply them together and does not imply an automatic workspace merge.
L07's bounded admission counts every accepted request up to `INT_MAX`.
M00 intentionally refactors that mechanism to capacity one so periodic demand coalesces instead of building a backlog.

## Elective tracks

Elective tracks belong in a separate course-level registry with generated E-series labels.
Their member exercises remain ordinary elective lessons; a track is a named, ordered bundle rather than a runnable exercise kind.

| Alias | Stable ID / title | Kind/status | Direct prerequisites | Teaches | Requires | Tradecraft | Sources | Assets |
|---|---|---|---|---|---|---|---|---|
| E01 | `kernel-debugging-track` — Kernel debugging | track/elective | `module-build-lifecycle` | `kernel-debugging` | `module-build` | instrumentation, detection | L03 | reuse/adapt only for new naming |
| E02 | `binary-control-abi-track` — Alternate binary control ABI | track/elective | `bounded-control-switch` | `character-devices`, `ioctl-dispatch`, `versioned-uapi`, `compat-ioctl` | `bounded-user-copy`, `file-descriptor-basics` | control | L14, L15 | adapt; versioned/compat contract is new |
| E03 | `usermode-helper-boundary-track` — Fixed userspace-helper boundary | track/elective | `proc-publication` | `usermode-helper` | `resource-ownership`, `linux-process-basics` | execution, detection | L16 | adapt; remove false proc-write dependency |
| E04 | `advanced-concurrency-diagnostics-track` — Advanced concurrency and diagnostics | track/elective | `event-driven-kernel-thread` | `completions`, `dead-man-watchdog`, `lock-ordering`, `memory-ordering`, `reference-counting`, `kernel-concurrency-diagnostics` | `state-invariant-selection`, `worker-stop-lifecycle` | lifecycle, execution, detection | L21, C02, portions L20/L23 | new beyond partial sources |
| E05 | `alternate-instrumentation-track` — Alternate instrumentation | track/elective | `managed-kprobe` | `fprobes`, `ftrace`, `tracepoints`, `defensive-bpf-comparison` | `managed-hook-lifecycle` | instrumentation, detection | L31 | new beyond fprobe material |
| E06 | `alternate-views-storage-track` — Alternate views and hidden storage | track/elective | `packed-directory-filter`, `bounded-rendered-filter` | `log-view-filtering`, `socket-row-filtering`, `sealed-dead-drop` | `directory-filtering`, `bounded-rendered-filter`, `cross-view-evidence` | concealment, detection | L38, L39, C06 | adapt |
| E07 | `alternate-packet-channels-track` — Alternate packet channels and helper handoff | track/elective | `safe-skb-access`, `kernel-socket-ownership` | `icmp-triggers`, `icmp-covert-messaging`, `raw-icmp-sockets`, `controlled-descriptor-handoff`, `tcp-shell-analysis` | `skb-safe-access`, `socket-lifecycle` | networking, control, detection | L42, raw portion L43, L45, L46 | adapt |
| E08 | `crypto-rendezvous-extensions-track` — Cryptographic and rendezvous extensions | track/elective | `authenticated-encryption`, `authenticated-dns-signal` | `xor-artifact-analysis`, `keyed-domain-generation`, `nxdomain-detection`, `sinkhole-analysis` | `protection-property-selection`, `authenticated-encryption`, `authenticated-dns-signals` | cryptography, networking, detection | L49, L50, L53 | adapt |

## Concept-registry changes

Add these genuinely new concepts:

- `c-functions-and-errors`
- `shell-and-paths`
- `host-guest-kernel-boundary`
- `c-preprocessor`
- `c-pointers-and-structs`
- `linux-process-basics`
- `linux-identity-basics`
- `ipv4-packet-basics`
- `transport-endpoint-basics`
- `dns-name-basics`
- `linux-file-permission-basics`
- `file-descriptor-basics`
- `module-inspection`
- `component-ownership`
- `errno-propagation`
- `resource-ownership`
- `pointer-invalidation`
- `build-time-failure-injection`
- `proc-publication-lifetime`
- `bounded-read-contract`
- `copy-validate-commit`
- `fixed-command-grammar`
- `action-control-switch`
- `bounded-typed-parsing`
- `state-invariant-selection`
- `read-once-write-once` — label: One-copy shared scalar access; first taught: L05
- `bounded-event-admission` — label: Bounded event admission; first taught: L07
- `timer-to-worker-deferral`
- `netfilter-callback-constraints`
- `kernel-socket-context-constraints`
- `kernel-file-context-constraints`
- `deferred-work-lifecycle`
- `worker-stop-lifecycle`
- `task-attribution`
- `namespace-id-translation`
- `namespaced-capabilities`
- `one-use-authorization`
- `authorization-vs-authentication`
- `replay-rejection`
- `sensitive-state-zeroization`
- `kernel-abi-contract`
- `managed-hook-lifecycle`
- `nofault-user-copy`
- `return-correlation`
- `cross-view-evidence`
- `positive-negative-controls`
- `packed-record-validation`
- `overlap-safe-record-compaction`
- `directory-cookie-preservation`
- `false-eof-avoidance`
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

- The broad `c-programming`, `unix-shell`, `linux-processes-and-files`, and
  `networking-fundamentals` assumptions are replaced by the narrower entry
  competencies above.
- `multi-file-modules` remains a mechanism but is taught with
  `component-ownership` inside `owned-kernel-state`.
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

`module-list-hiding` and `sysfs-artifacts` have no v3 consumer and are retired at cutover.
The proposed `reversible-view-suppression` concept is not added.
`kernel-lists` also leaves the required route; retain it only if a completed elective teaches and uses intrusive lists.
The removed `module-view-suppression` checkpoint receives no replacement alias.

## Asset reality

No complete target asset set currently exists for the redesigned route.

- Independently validated incubation assets exist for L00 through L14 and for M00 and M01.
- L15 has an implemented incubation asset awaiting independent audit.
- These assets are not yet the public route or final metadata.

- L16 through L30 still require target assets satisfying their contracts.
- M02, M03, and the final capstone still require cumulative checks, hints, and
  sparse solution overlays on the declared project lineage.
- The authenticated DNS signal source lacks the required replay-safe state machine.
- E02, E04, E05, and E08 substantially exceed their current source exercises.
- Existing source bundles are inputs for adaptation.
  Merged checks must not be
  presented as target checks without rewriting their observable and negative
  contracts.
