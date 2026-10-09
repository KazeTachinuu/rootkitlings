# Rootkitlings curriculum knowledge graph

Status: internal working research draft.
It describes a proposed future curriculum, not the currently implemented learner route.
Preserve it until the redesign is complete.

## Course promise

Learn Linux kernel development by building a rootkit one mechanism at a time.

Every required lesson must introduce one transferable mechanism, apply it to a concrete rootkit subsystem, establish its ownership and failure invariant, expose independent defensive evidence, and produce a capability reused later.

## Scope

- Linux loadable kernel modules on one pinned kernel and architecture.
- Disposable, isolated virtual machine with explicit authorization and cleanup.
- Kernel development taught through rootkit mechanisms and defensive validation.
- No exploit development, initial compromise, firmware rootkits, bootkits, hypervisor rootkits, or production deployment.
- No claim that Linux internal module APIs are stable across kernel versions.

## Overview

```text
Authorized lab
|
+-- Linux and C prerequisites
|   +-- compilation, ELF and linking
|   +-- pointers, structs and callbacks
|   +-- binary data, bounds and endianness
|   +-- ownership and object lifetime
|
+-- Kernel module foundation
|   +-- Kbuild and module metadata
|   +-- init, exit, load and unload
|   +-- errors and transactional rollback
|   +-- memory and resource ownership
|   +-- logging, debugging and testing
|
+-- Kernel/userspace boundary
|   +-- procfs and seq_file
|   +-- sysfs and parameters
|   +-- character devices and ioctl
|   +-- bounded user-memory access
|   +-- control-plane parsing
|
+-- Execution and concurrency
|   +-- execution contexts
|   +-- timers and workqueues
|   +-- kernel threads
|   +-- one-copy scalar access
|   +-- atomic read-modify-write, mutexes and spinlocks
|   +-- wait queues and completions
|   +-- memory ordering
|   +-- shutdown and callback quiescence
|
+-- Task identity and privilege
|   +-- current task
|   +-- credentials and namespaces
|   +-- caller attribution
|   +-- transactional credential replacement
|
+-- Instrumentation
|   +-- symbols and architecture ABI
|   +-- kprobe entry observation
|   +-- kretprobe return observation
|   +-- ftrace and fprobe alternatives
|   +-- recursion and missed events
|   +-- multi-hook rollback
|
+-- View manipulation and concealment
|   +-- object versus rendered view
|   +-- module and sysfs views
|   +-- directory enumeration
|   +-- file-read results
|   +-- log-reader results
|   +-- network-table rendering
|   +-- independent cross-view detection
|
+-- Networking
|   +-- IPv4, ICMP, UDP, TCP and DNS
|   +-- sk_buff geometry and ownership
|   +-- Netfilter lifecycle
|   +-- bounded packet parsing
|   +-- checksums and safe mutation
|   +-- kernel socket ownership
|   +-- framing, partial I/O and cancellation
|
+-- Cryptographic properties
|   +-- XOR obfuscation
|   +-- randomness and key lifecycle
|   +-- hashing
|   +-- HMAC authentication
|   +-- authenticated encryption
|   +-- nonces, tags, freshness and replay
|   +-- Kernel Crypto API
|
+-- Rootkit subsystems
|   +-- bounded action control switch
|   +-- scheduler and resident worker
|   +-- privileged action
|   +-- telemetry and event capture
|   +-- concealment policy
|   +-- remote fixed-function agent
|   +-- persistence transaction
|   +-- authenticated signaling
|
+-- Defensive validation
    +-- static module inspection
    +-- runtime artifact inventory
    +-- positive and negative controls
    +-- cross-view comparison
    +-- failure injection
    +-- race and unload stress
    +-- sanitizers and lock validation
    +-- complete cleanup verification
```

## Safety and authorization

```text
SAFE-01  Explicit authorization
SAFE-02  Named system owner and learner identity
SAFE-03  Exact permitted VM and image
SAFE-04  Network and filesystem scope
SAFE-05  Explicit prohibited targets
SAFE-06  Rules of engagement
SAFE-07  Stop and incident criteria
SAFE-08  Disposable snapshot and restoration
SAFE-09  No production credentials or data
SAFE-10  Evidence-retention policy
SAFE-11  Cleanup contract
SAFE-12  Threat model and trust boundaries
```

```text
authorization
    -> scope
        -> containment
            -> rules of engagement
                -> stop criteria
                -> evidence plan
                -> cleanup plan
```

## Baseline Linux knowledge

```text
BASE-01  Shell commands, quoting and pipelines
BASE-02  Files, paths, permissions and ownership
BASE-03  Processes, threads, PIDs and signals
BASE-04  File descriptors and open-file lifetime
BASE-05  Users, groups, UID and GID
BASE-06  Capabilities and privileged operations
BASE-07  Userspace versus kernel space
BASE-08  Virtual filesystems: procfs and sysfs
BASE-09  IPv4 addresses and network byte order
BASE-10  ICMP, UDP, TCP and DNS vocabulary
BASE-11  QEMU/KVM guest versus host boundary
BASE-12  Reproducible debugging and testing
```

These are course-entry expectations and must be declared once rather than duplicated in every exercise.

## C, binary representation and ABI

```text
C-01  Declarations, definitions and scope
C-02  Static storage duration
C-03  const correctness
C-04  Signed and unsigned integers
C-05  size_t, ssize_t and negative errno
C-06  Fixed-width kernel integer types
C-07  Integer conversions and overflow
C-08  Hexadecimal and bit operations
C-09  Pointers and null
C-10  Pointer arithmetic
C-11  Arrays and array decay
C-12  Counted bytes versus C strings
C-13  Termination and embedded NUL
C-14  Structs, unions and enums
C-15  Padding and alignment
C-16  sizeof and offsetof
C-17  Intrusive container relationships
C-18  Function pointers and callbacks
C-19  Operation tables
C-20  Headers and translation units
C-21  Include guards and shared contracts
C-22  Preprocessor macros
C-23  Macro side effects
C-24  Object lifetime and aliasing
C-25  Use-after-free and double-free models
C-26  Endianness and byte order
C-27  Stable binary layouts
C-28  32-bit compatibility on a 64-bit kernel
C-29  Architecture calling convention
C-30  Compiler warnings as correctness evidence
```

```text
integers + pointers
    -> arrays and byte sequences
        -> structs and alignment
            -> callbacks and operation tables
                -> kernel interfaces
                -> architecture ABI
```

## Build system, ELF and module mechanics

```text
MOD-01  Matching kernel source, headers and configuration
MOD-02  External-module Kbuild
MOD-03  obj-m and composite module objects
MOD-04  Preprocess, compile, link and modpost
MOD-05  ELF sections
MOD-06  Symbol tables and relocations
MOD-07  Exported versus internal symbols
MOD-08  Module.symvers and symbol versioning
MOD-09  Module namespaces and dependencies
MOD-10  vermagic compatibility
MOD-11  MODULE_LICENSE and metadata
MOD-12  Module parameters
MOD-13  Module signing
MOD-14  Kernel lockdown and load policy
MOD-15  init_module/finit_module loading
MOD-16  delete_module unloading
MOD-17  module_init and module_exit
MOD-18  Module reference counts
MOD-19  Externally built and unsigned-module taint
MOD-20  Static inspection with file/modinfo/strings/nm
```

## Lifecycle, errors and ownership

```text
LIFE-01  Successful init transfers ownership to exit
LIFE-02  Failed init does not invoke exit
LIFE-03  Negative errno convention
LIFE-04  Positive count, zero EOF, negative error
LIFE-05  NULL versus ERR_PTR versus valid pointer
LIFE-06  IS_ERR, PTR_ERR and IS_ERR_OR_NULL
LIFE-07  Preserve original failure reasons
LIFE-08  One owner per resource
LIFE-09  Explicit acquisition order
LIFE-10  Reverse-order destruction
LIFE-11  Partial-initialization rollback
LIFE-12  Fail-at-each-stage validation
LIFE-13  Pointer invalidation after release
LIFE-14  Idempotence where APIs require it
LIFE-15  Unregister before freeing dependencies
LIFE-16  Stop asynchronous execution before unload
LIFE-17  Wait for callback quiescence
LIFE-18  Open-handle and module-reference ownership
LIFE-19  Repeated load/unload safety
LIFE-20  Cleanup from independent evidence
```

```text
resource acquisition
    -> ownership
        -> acquisition ordering
            -> reverse teardown
                -> partial failure rollback
                -> unload safety
                    -> callback quiescence
```

## Memory and data structures

```text
MEM-01  Stack, static and heap lifetime
MEM-02  kmalloc, kzalloc and kfree
MEM-03  Allocation failure
MEM-04  Checked allocation-size arithmetic
MEM-05  GFP_KERNEL and sleeping allocation
MEM-06  Non-sleeping allocation context
MEM-07  Flexible arrays and bounded storage
MEM-08  Kernel linked-list model
MEM-09  container_of
MEM-10  Safe list traversal and deletion
MEM-11  List ownership
MEM-12  Concurrent list protection
MEM-13  Reference counting
MEM-14  Locking versus lifetime references
MEM-15  RCU read-side protection
MEM-16  RCU grace periods
MEM-17  RCU-safe module unload
MEM-18  Per-CPU state
MEM-19  Zeroization of sensitive temporary state
```

## Kernel/userspace boundary

```text
USER-01  __user address-space annotation
USER-02  Never directly dereference userspace pointers
USER-03  copy_from_user residual-count semantics
USER-04  copy_to_user residual-count semantics
USER-05  get_user and put_user
USER-06  Translate copy failure to -EFAULT
USER-07  Validate bounds before copying
USER-08  Copy into local storage before parsing
USER-09  Terminate only after successful bounded copy
USER-10  Copy-then-commit transaction
USER-11  Preserve state on rejected input
USER-12  Userspace can mutate memory concurrently
USER-13  Avoid double-fetch and TOCTOU
USER-14  No-fault access in restricted contexts
USER-15  Exact command matching
USER-16  Reject unknown commands
USER-17  Permissions do not replace validation
USER-18  Compat ABI
```

## VFS and control interfaces

```text
VFS-01  Superblock, inode, dentry and file distinctions
VFS-02  Open file instance versus filesystem object
VFS-03  file_operations lifetime
VFS-04  Read/write return contracts
VFS-05  File position and repeated reads
VFS-06  Short reads and writes
VFS-07  procfs publication and removal
VFS-08  procfs permissions
VFS-09  seq_file iterator model
VFS-10  seq_file short-read and seek behavior
VFS-11  Bounded procfs writes
VFS-12  sysfs and kobject publication
VFS-13  Module parameters
VFS-14  Parameter callbacks
VFS-15  Sysfs attributes are not automatically stable UAPI
VFS-16  Misc and character-device registration
VFS-17  Device open and release
VFS-18  ioctl command encoding
VFS-19  ioctl direction, type, number and size
VFS-20  Unknown ioctl returns -ENOTTY
VFS-21  Fixed-width versioned UAPI
VFS-22  Netlink as an advanced structured control alternative
```

## Execution contexts and concurrency

```text
SYNC-01  Process context
SYNC-02  Atomic context
SYNC-03  Interrupt and softirq context
SYNC-04  Timer callback context
SYNC-05  Probe callback context
SYNC-06  NMI awareness
SYNC-07  Sleepable versus non-sleepable APIs
SYNC-08  Preemption and SMP concurrency
SYNC-09  Data race versus logical invariant
SYNC-10  Lifetime race
SYNC-11  READ_ONCE and WRITE_ONCE scalar access and limitations
SYNC-12  Atomic read-modify-write and bounded event admission
SYNC-13  Atomics protect one scalar, not a transaction
SYNC-14  Mutexes and sleepable critical sections
SYNC-15  Spinlocks and no-sleep critical sections
SYNC-16  Lock scope
SYNC-17  Lock ordering and deadlock
SYNC-18  Memory ordering
SYNC-19  Acquire and release
SYNC-20  Publish condition before wakeup
SYNC-21  Timer scheduling and synchronous cancellation
SYNC-22  Workqueue deferral
SYNC-23  Work coalescing
SYNC-24  Work cancellation and flushing
SYNC-25  Kernel-thread creation and ERR_PTR handling
SYNC-26  kthread_should_stop
SYNC-27  kthread_stop as stop plus join
SYNC-28  Wait-queue condition loops
SYNC-29  Interruptible wait errors
SYNC-30  Completions for finite rendezvous
SYNC-31  Reference lifetime across asynchronous work
```

## Tasks, credentials and authorization

```text
TASK-01  struct task_struct
TASK-02  current and execution context
TASK-03  PID and comm attribution
TASK-04  Task lifetime and references
TASK-05  struct cred snapshots
TASK-06  Credentials are immutable after publication
TASK-07  Real, effective, saved and filesystem IDs
TASK-08  Supplementary groups
TASK-09  Capabilities
TASK-10  User namespaces and ID translation
TASK-11  current_cred and caller attribution
TASK-12  prepare_creds
TASK-13  Populate a complete replacement credential
TASK-14  commit_creds
TASK-15  abort_creds on failure
TASK-16  A task changes its own credentials
TASK-17  Authentication versus authorization
TASK-18  Same-caller privileged action
TASK-19  One-use authorization state
```

## Symbols and instrumentation

```text
TRACE-01  Kernel internal API instability
TRACE-02  Symbol names versus addresses
TRACE-03  Exported-symbol restrictions
TRACE-04  Probeable versus blacklisted targets
TRACE-05  Architecture register ABI
TRACE-06  Syscall-wrapper argument decoding
TRACE-07  Kprobe registration lifecycle
TRACE-08  Entry observation
TRACE-09  Saved-register access
TRACE-10  Kretprobe entry/return correlation
TRACE-11  Return-value observation
TRACE-12  maxactive and missed instances
TRACE-13  Ftrace framework
TRACE-14  Tracefs visibility
TRACE-15  Fprobe multi-function attachment
TRACE-16  Probe recursion
TRACE-17  Reentrancy and per-CPU execution
TRACE-18  Restricted callback behavior
TRACE-19  Bounded event capture
TRACE-20  Multi-hook acquisition transaction
TRACE-21  Unregister before state release
TRACE-22  Probe registry as defensive evidence
TRACE-23  Miss counters as evidence quality
TRACE-24  Tracepoints as a more stable alternative
TRACE-25  BPF-based observation as a defensive comparison
```

## View manipulation and concealment

The atomic concept is that a kernel object and a rendered view of that object are different things.

```text
VIEW-01  Underlying object versus published view
VIEW-02  One altered view does not remove the object
VIEW-03  Module enumeration versus module sysfs object
VIEW-04  Kernel object publication and removal
VIEW-05  Directory enumeration versus exact lookup
VIEW-06  Packed variable-length directory records
VIEW-07  Minimum record length
VIEW-08  Forward-progress validation
VIEW-09  Overlap-safe record compaction
VIEW-10  Correct returned-length adjustment
VIEW-11  File-read result versus durable file bytes
VIEW-12  Inode-scoped policy
VIEW-13  Equal-length substitution
VIEW-14  Log-reader output versus retained or external logs
VIEW-15  Line-oriented bounded compaction
VIEW-16  Network-table row versus live socket
VIEW-17  seq_file row rendering
VIEW-18  Process enumeration versus scheduler/task evidence
VIEW-19  Artifact creation, removal and recreation
VIEW-20  Cross-view validation
VIEW-21  Independent evidence source
VIEW-22  Before, stimulus and after comparison
VIEW-23  Positive and negative controls
VIEW-24  Cleanup observation
```

## Networking and packet processing

### Protocol foundations

```text
NET-01  Octets and network byte order
NET-02  Bounds and overflow-aware length arithmetic
NET-03  IPv4 version and variable header length
NET-04  IPv4 total length and fragmentation
NET-05  Protocol demultiplexing
NET-06  ICMP type, code, identifier and payload
NET-07  UDP length and datagram boundaries
NET-08  TCP connection lifecycle
NET-09  TCP is a byte stream, not a message stream
NET-10  DNS header and question structure
NET-11  DNS length-prefixed labels
NET-12  DNS label and name limits
NET-13  DNS compression awareness when parsing
```

### Kernel packet representation

```text
SKB-01  sk_buff is metadata plus packet storage
SKB-02  Linear and fragmented data
SKB-03  Shared and cloned buffers
SKB-04  Header offsets
SKB-05  Safe read helpers
SKB-06  Validate before access
SKB-07  Fragment handling policy
SKB-08  Writability and copy-on-write
SKB-09  Reacquire pointers after mutation
SKB-10  Checksum fields
SKB-11  Checksum offload metadata
SKB-12  Length and checksum consistency after mutation
```

### Netfilter

```text
NF-01  Packet traversal points
NF-02  PRE_ROUTING
NF-03  LOCAL_IN
NF-04  FORWARD
NF-05  LOCAL_OUT
NF-06  POST_ROUTING
NF-07  Network-namespace scope
NF-08  Hook registration and rollback
NF-09  Explicit verdict semantics
NF-10  Default non-disruptive observation
NF-11  Callback context restrictions
NF-12  Bounded classification
NF-13  Safe mutation
NF-14  Hook registration as observable evidence
```

### Kernel sockets

```text
SOCK-01  Address family, type and protocol
SOCK-02  Namespace-aware socket creation
SOCK-03  Error-pointer handling
SOCK-04  Bind, connect, listen and accept states
SOCK-05  Socket ownership
SOCK-06  Shutdown versus release
SOCK-07  Kernel send and receive vectors
SOCK-08  Datagram exactness
SOCK-09  TCP frame grammar
SOCK-10  Partial-read accumulation
SOCK-11  Oversized-frame draining and rejection
SOCK-12  Partial-send progress loop
SOCK-13  Publish active socket for cancellation
SOCK-14  Stop-aware blocked I/O
SOCK-15  Reconnect and bounded backoff
SOCK-16  Health versus merely open connection
SOCK-17  Socket tables as evidence
SOCK-18  Packet capture as independent evidence
```

## Cryptographic concepts

XOR is an honest obfuscation primer, not encryption.

```text
CRYPTO-01  Representation versus protection
CRYPTO-02  Encoding
CRYPTO-03  Reversible transformation
CRYPTO-04  Fixed or repeating XOR obfuscation
CRYPTO-05  Why XOR is not confidentiality
CRYPTO-06  Confidentiality
CRYPTO-07  Integrity
CRYPTO-08  Origin authentication
CRYPTO-09  Freshness
CRYPTO-10  Cryptographic randomness
CRYPTO-11  Key generation and provisioning
CRYPTO-12  Key exposure and rotation
CRYPTO-13  Sensitive-state zeroization
CRYPTO-14  Hash and digest
CRYPTO-15  Trusted reference digest
CRYPTO-16  Why an unkeyed hash is not authentication
CRYPTO-17  MAC and HMAC
CRYPTO-18  Constant-time tag comparison
CRYPTO-19  Symmetric encryption
CRYPTO-20  Why encryption alone is insufficient
CRYPTO-21  AEAD
CRYPTO-22  Associated data
CRYPTO-23  Authentication tag
CRYPTO-24  Nonce and IV requirements
CRYPTO-25  Nonce reuse hazard
CRYPTO-26  Tag-length security budget
CRYPTO-27  Replay defense
CRYPTO-28  Sequence, time and challenge state
CRYPTO-29  Fail-closed authentication
```

### Kernel Crypto API

```text
KCRYPTO-01  Algorithm class and name
KCRYPTO-02  Generic algorithm versus implementation driver
KCRYPTO-03  Transform allocation and ERR_PTR handling
KCRYPTO-04  Transform configuration and release
KCRYPTO-05  Synchronous hash descriptor
KCRYPTO-06  Digest init, update and final
KCRYPTO-07  HMAC key setup
KCRYPTO-08  Per-operation request lifetime
KCRYPTO-09  Scatterlists
KCRYPTO-10  AEAD input and output layout
KCRYPTO-11  Associated-data length
KCRYPTO-12  IV storage and lifetime
KCRYPTO-13  Authentication-tag size
KCRYPTO-14  Async callback and completion
KCRYPTO-15  -EINPROGRESS and backlog
KCRYPTO-16  -EBADMSG means authentication failure
KCRYPTO-17  Never expose unauthenticated plaintext
KCRYPTO-18  Known-answer vectors
KCRYPTO-19  Modified message, tag, key and AAD tests
```

## Control plane and agent behavior

The curriculum uses fixed, bounded commands rather than an arbitrary execution framework.

```text
CTRL-01  Control plane versus data plane
CTRL-02  Telemetry plane
CTRL-03  Minimal enumerated command vocabulary
CTRL-04  Typed command arguments
CTRL-05  Maximum frame size
CTRL-06  Complete-frame validation
CTRL-07  Unknown-command rejection
CTRL-08  Authentication before action
CTRL-09  Authorization after authentication
CTRL-10  Freshness and replay policy
CTRL-11  Session lifecycle
CTRL-12  One owner for connection resources
CTRL-13  Stop-aware retry
CTRL-14  Health and liveness state
CTRL-15  Attempts, successes and failures
CTRL-16  Last error and transition time
CTRL-17  Primary and fallback distinction
CTRL-18  Bounded DNS-shaped signaling
CTRL-19  Dynamic rendezvous concept
CTRL-20  Blocking, sinkholing and anomaly detection
CTRL-21  No arbitrary shell, eval or plugin transfer
```

## Persistence

```text
PERSIST-01  Runtime state is not persistence
PERSIST-02  Durable state and next-boot semantics
PERSIST-03  Bounded original-state capture
PERSIST-04  Exact intended-image construction
PERSIST-05  Exact write-count validation
PERSIST-06  Ownership classification
PERSIST-07  Foreign-state rejection
PERSIST-08  Comparison before rollback
PERSIST-09  Never overwrite third-party changes
PERSIST-10  Boot-time module request
PERSIST-11  Requested module versus installed module binary
PERSIST-12  Module signing and load-policy interaction
PERSIST-13  Cleanup from durable state
PERSIST-14  Reboot and next-boot verification
PERSIST-15  Defensive module-load restriction
PERSIST-16  Secure and measured boot boundary
```

## Rootkit component architecture

```text
rootkit_main
|
+-- control
|   +-- parser
|   +-- Boolean action gate
|   +-- authorization
|
+-- scheduler
|   +-- timer
|   +-- bounded timer producer
|
+-- request path
|   +-- capacity-one atomic admission
|   +-- proc producer
|
+-- worker
|   +-- thread
|   +-- wait queue
|   +-- stop protocol
|
+-- privilege
|   +-- one-use token
|   +-- credential transaction
|
+-- telemetry
|   +-- bounded event record
|   +-- spinlocked ring
|   +-- safe snapshot
|
+-- instrumentation
|   +-- target selection
|   +-- probes
|   +-- callback lifecycle
|
+-- concealment
|   +-- policy
|   +-- one altered view
|   +-- independent evidence
|
+-- agent
|   +-- socket lifecycle
|   +-- framing
|   +-- fixed commands
|   +-- retry and cancellation
|
+-- persistence
|   +-- owned file transaction
|   +-- comparison-before-rollback
|
+-- authentication
    +-- key state
    +-- HMAC or AEAD
    +-- freshness and replay state
```

Each component should expose only direct lifecycle functions and its narrow operational API.
Avoid generic plugin managers, dependency injection, event buses and abstract resource frameworks.

## Testing and defensive evidence

```text
TEST-01  State the invariant before implementing
TEST-02  Host/static test versus guest/runtime test
TEST-03  Unit-test pure parsers and crypto adapters
TEST-04  VM-test kernel integration
TEST-05  Known-answer vectors
TEST-06  Empty, minimum, maximum and one-over boundaries
TEST-07  Truncation at every record boundary
TEST-08  Embedded-NUL behavior
TEST-09  Invalid pointer and error path
TEST-10  Unknown command
TEST-11  Wrong key, tag, AAD and message
TEST-12  Fail every acquisition stage
TEST-13  Verify reverse cleanup
TEST-14  Repeat load and unload
TEST-15  Unload during active callbacks
TEST-16  Stop during blocked socket I/O
TEST-17  Concurrent control and status access
TEST-18  Exact scalar and compound-state invariants
TEST-19  Cross-view evidence
TEST-20  Packet-capture oracle
TEST-21  Durable-file comparison
TEST-22  No residual hook, object, socket or file
TEST-23  Pinned kernel, configuration and toolchain
TEST-24  Deterministic timeouts
TEST-25  Destructive-test restoration
TEST-26  Regression contracts for earlier milestones
TEST-27  Name the owning subsystem on regression
```

### Kernel validation tools

```text
KUnit     internal unit testing
KASAN     out-of-bounds and use-after-free
KCSAN     data races
KMSAN     uninitialized values
lockdep   locking-order and context defects
sparse    address-space and type annotations
FORTIFY   bounded memory and string operations
fault injection
ftrace and tracefs
kmemleak
```

## Final dependency spine

```text
Authorization and containment
    |
Linux + C + binary representation
    |
Kbuild + module lifecycle
    |
Errors + ownership + rollback
    |
Memory + user boundary + VFS interfaces
    |
Execution contexts + synchronization
    |
Task identity + credentials
    |
Symbols + instrumentation
    |
Object/view separation
    |
Concealment + cross-view detection
    |
Packet parsing + Netfilter + sockets
    |
Hash -> HMAC -> AEAD
    |
Authenticated fixed-function control
    |
Durable persistence transaction
    |
Cumulative rootkit integration
    |
Adversarial validation and complete cleanup
```

## Primary references

### Linux kernel

- [Building external modules](https://docs.kernel.org/kbuild/modules.html)
- [Driver basics and module lifecycle](https://docs.kernel.org/driver-api/basics.html)
- [Kernel memory allocation](https://docs.kernel.org/core-api/memory-allocation.html)
- [Kernel linked lists](https://docs.kernel.org/core-api/list.html)
- [RCU handbook](https://docs.kernel.org/RCU/index.html)
- [VFS](https://docs.kernel.org/filesystems/vfs.html)
- [procfs](https://docs.kernel.org/filesystems/proc.html)
- [seq_file](https://docs.kernel.org/filesystems/seq_file.html)
- [sysfs](https://docs.kernel.org/filesystems/sysfs.html)
- [Kernel credentials](https://docs.kernel.org/security/credentials.html)
- [Lock types](https://docs.kernel.org/locking/locktypes.html)
- [Workqueues](https://docs.kernel.org/core-api/workqueue.html)
- [Completions](https://docs.kernel.org/scheduler/completion.html)
- [Kprobes and kretprobes](https://docs.kernel.org/trace/kprobes.html)
- [Fprobe](https://docs.kernel.org/trace/fprobe.html)
- [Tracing guide](https://docs.kernel.org/trace/index.html)
- [`sk_buff`](https://docs.kernel.org/networking/skbuff.html)
- [Networking APIs](https://docs.kernel.org/networking/kapi.html)
- [Kernel Crypto API](https://docs.kernel.org/crypto/intro.html)
- [Digest API](https://docs.kernel.org/crypto/api-digest.html)
- [AEAD API](https://docs.kernel.org/crypto/api-aead.html)
- [Module signing](https://docs.kernel.org/admin-guide/module-signing.html)
- [Tainted kernels](https://docs.kernel.org/admin-guide/tainted-kernels.html)
- [Kernel self-protection](https://docs.kernel.org/security/self-protection.html)
- [Kernel testing overview](https://docs.kernel.org/dev-tools/testing-overview.html)
- [KUnit](https://docs.kernel.org/dev-tools/kunit/)
- [KASAN](https://docs.kernel.org/dev-tools/kasan.html)
- [KCSAN](https://docs.kernel.org/dev-tools/kcsan.html)
- [KMSAN](https://docs.kernel.org/dev-tools/kmsan.html)

### Protocols and cryptography

- [IPv4: RFC 791](https://www.rfc-editor.org/info/rfc791)
- [ICMP: RFC 792](https://www.rfc-editor.org/info/rfc792)
- [UDP: RFC 768](https://www.rfc-editor.org/info/rfc768)
- [TCP: RFC 9293](https://www.rfc-editor.org/info/rfc9293)
- [DNS: RFC 1035](https://www.rfc-editor.org/info/rfc1035)
- [NIST Secure Hash Standard](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)
- [NIST message-authentication codes](https://csrc.nist.gov/projects/message-authentication-codes)
- [NIST GCM and GMAC](https://csrc.nist.gov/pubs/sp/800/38/d/final)

### Threat, authorization and detection vocabulary

- [NIST SP 800-115](https://csrc.nist.gov/pubs/sp/800/115/final)
- [MITRE ATT&CK: Rootkit](https://attack.mitre.org/techniques/T1014/)
- [MITRE ATT&CK: Kernel Modules and Extensions](https://attack.mitre.org/techniques/T1547/006/)
- [MITRE ATT&CK: Command and Control](https://attack.mitre.org/tactics/TA0011/)
- [MITRE ATT&CK: DNS](https://attack.mitre.org/techniques/T1071/004/)
- [MITRE ATT&CK: Fallback Channels](https://attack.mitre.org/techniques/T1008/)

## Redesign notes

The current registry captures lesson outcomes but does not yet represent all prerequisite atoms in this document.
Important gaps include execution contexts, memory ordering, reference lifetime, RCU, compat ABI, packet fragmentation, replay protection, module-load policy and sanitizer-backed validation.

This draft must be reconciled into a smaller set of learner-facing concepts.
Do not copy every atom into every exercise.
Course-entry expectations, direct lesson prerequisites, inherited concepts, implementation details and defensive evidence must remain separate metadata dimensions.
