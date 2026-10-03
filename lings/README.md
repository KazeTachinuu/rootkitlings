# Rootkitlings roadmap

The course uses rootkit techniques to teach Linux kernel engineering and the
corresponding defensive view. Each ling introduces one mechanism, one ownership
rule, and one observable behavior. Reference code targets Linux 7.2.

## Sequence

| Category | Lings | Engineering focus |
|----------|------:|-------------------|
| [Foundation](00_foundation/README.md) | 00-10 | Module lifecycle, builds, failure rollback, allocation, procfs |
| [Interfaces](01_interfaces/README.md) | 11-16 | Error pointers, writes, lists, devices, ioctl, userspace helpers |
| [Concurrency](02_concurrency/README.md) | 17-23 | Timers, workqueues, kthreads, waits, races, mutexes, atomics |
| [Introspection](03_introspection/README.md) | 24-28 | Tasks, credentials, symbol resolution, kernel randomness |
| [Hooking](04_hooking/README.md) | 29-35 | Debugfs, kprobe, kretprobe, page protection, fprobe, lifecycle |
| [Hiding](05_hiding/README.md) | 36-42 | Module, sysfs, directory, content, log, process, and port visibility |
| [Networking](06_networking/README.md) | 43-51 | Netfilter, packet parsing, sockets, channels, counters, rewriting |
| [Persistence](07_persistence/README.md) | 52-56 | Triggers, userspace handoff, file changes, boot state, recovery |
| [Crypto](08_crypto/README.md) | 57-59 | Obfuscation, encryption, integrity verification |

Six [capstones](../capstones/README.md) test integration after lings 16, 23,
35, 42, 51, and 59.

## Learning flow

The numbered route is deliberate: each row uses vocabulary and ownership rules
introduced above it. Capstones sit at the points where several mechanisms can
be combined without introducing new theory.

```text
      +--------------------------+
      | FOUNDATION         00-10 |  lifecycle, build, memory, procfs
      +-------------+------------+
                    |
                    v
      +--------------------------+
      | INTERFACES         11-16 |  user/kernel boundaries        ---> CAPSTONE 0: CONFIG STORE
      +-------------+------------+
                    |
                    v
      +--------------------------+
      | CONCURRENCY        17-23 |  timers, workers, locking      ---> CAPSTONE 1: BEACON
      +-------------+------------+
                    |
                    v
      +--------------------------+
      | INTROSPECTION      24-28 |  tasks, credentials, symbols
      +-------------+------------+
                    |
                    v
      +--------------------------+
      | HOOKING            29-35 |  probes and hook lifecycle     ---> CAPSTONE 2: TRIPWIRE
      +-------------+------------+
                    |
                    v
      +--------------------------+
      | HIDING             36-42 |  filtered system views         ---> CAPSTONE 3: DEAD DROP
      +-------------+------------+
                    |
                    v
      +--------------------------+
      | NETWORKING         43-51 |  packets, sockets, channels    ---> CAPSTONE 4: IMPLANT
      +-------------+------------+
                    |
                    v
      +--------------------------+
      | PERSISTENCE        52-56 |  triggers, boot, recovery
      +-------------+------------+
                    |
                    v
      +--------------------------+
      | CRYPTO             57-59 |  encryption and integrity      ---> CAPSTONE 5: RED VS BLUE
      +--------------------------+
```

The direct path is `00` through `59`. Once the fundamentals are comfortable, the
category boundaries make it easy to revisit a specific mechanism without
replaying the entire course.

## Minimal rootkit (5 lings)

The shortest path to a working rootkit that hides itself and intercepts files:

| Ling | What it gives you |
|-----:|-------------------|
| 00 | Module lifecycle (load, unload, dmesg) |
| 30 | Kprobe hooking |
| 31 | Kretprobe (intercept return values) |
| 36 | Module hiding (disappear from lsmod) |
| 38 | Directory listing filter (hide files/procs) |

After these five you can build a module that loads, hooks a syscall, hides
entries from directory listings, and removes itself from the module list.
Everything else (networking, persistence, crypto, proc interfaces) is
optional hardening.

## Rules for reference code

- Target the current course kernel, not a maze of historical version branches.
- Use the narrowest official API that expresses the lesson.
- Check every fallible operation that affects correctness.
- Acquire resources in order and release them in reverse order.
- Stop workers and unregister callbacks before freeing their state.
- Keep callbacks bounded; do sleeping work in a sleepable context.
- Test externally visible behavior, including cleanup after unload.
- Call out deliberate anti-patterns inside the lesson that demonstrates them.

## Real implementations

Diamorphine, Reptile, KoviD, PUMAKIT, and similar projects are case studies,
not style guides. Their version-specific bypasses show how offensive code reacts
to kernel hardening. The course first teaches supported primitives - kprobes,
kretprobes, fprobe, seq_file, netfilter, kthreads, and the crypto API - then shows
where real rootkits depart from supported interfaces and how defenders detect
that departure.

## Upstream references

- [External modules](https://docs.kernel.org/kbuild/modules.html)
- [Memory allocation](https://docs.kernel.org/core-api/mm-api.html)
- [Driver basics](https://docs.kernel.org/driver-api/basics.html)
- [seq_file](https://docs.kernel.org/filesystems/seq_file.html)
- [Lock types](https://docs.kernel.org/locking/locktypes.html)
- [Kprobes and kretprobes](https://docs.kernel.org/trace/kprobes.html)
- [Fprobe](https://docs.kernel.org/trace/fprobe.html)
- [Netfilter](https://docs.kernel.org/networking/netfilter.html)
- [Kernel crypto API](https://docs.kernel.org/crypto/index.html)
