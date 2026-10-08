# Lings

Lings use rootkit techniques to teach Linux kernel engineering and the corresponding defensive view.
Each one isolates a mechanism, its relevant lifetime or ownership rule, and observable behavior.

Reference code targets Linux 7.2.x.

## Progression

| Category | Lings | Engineering focus |
|----------|------:|-------------------|
| [Foundation](00_foundation/README.md) | 00-08 | Module lifecycle, builds, failure rollback, allocation |
| [Procfs](01_procfs/README.md) | 09-11 | Entry lifetime, bounded reads and writes |
| [Interfaces](02_interfaces/README.md) | 12-16 | Error pointers, lists, devices, ioctl, userspace helpers |
| [Concurrency](03_concurrency/README.md) | 17-23 | Timers, workqueues, kthreads, waits, mutexes, atomics |
| [Introspection](04_introspection/README.md) | 24-28 | Tasks, credentials, symbol resolution, randomness |
| [Instrumentation](05_instrumentation/README.md) | 29-33 | Kprobe, kretprobe, fprobe, arguments, lifecycle |
| [Visibility](06_visibility/README.md) | 34-39 | Module, sysfs, directory, content, log, and socket views |
| [Networking](07_networking/README.md) | 40-47 | Netfilter, packet parsing, sockets, channels, DNS framing |
| [File I/O](08_file_io/README.md) | 48 | Bounded file update and rollback |
| [Cryptography](09_crypto/README.md) | 49-53 | Obfuscation, AEAD, hashes, HMAC, deterministic domains |

The progression is deliberate: later work reuses vocabulary, execution-context constraints, and ownership rules introduced earlier.
Eleven [capstones](../capstones/README.md) are interleaved after the mechanisms they combine.

Every `exercise.toml` records what the exercise teaches, the concepts its marked task applies, and the smallest non-redundant set of earlier exercises that supplies those concepts.
Course-entry assumptions live once in `course.toml`; they are not repeated in every exercise.
Use `../rootkitlings list` to see the canonical 65-exercise route and `../rootkitlings status` to see your place in it.

## Engineering rules

- Target the course kernel instead of carrying historical version branches.
- Use the narrowest official API that expresses the lesson.
- Check every fallible operation that affects correctness.
- Acquire resources in order and release them in reverse order.
- Stop workers and unregister callbacks before freeing their state.
- Keep callbacks bounded; move sleeping work to a sleepable context.
- Test externally visible behavior, including cleanup after unload.
- Identify the independent view that exposes a concealment technique.

Real rootkits such as Diamorphine, Reptile, KoviD, and PUMAKIT are case studies, not style guides.
The course begins with supported primitives, then shows where offensive implementations depart from them and how defenders detect that departure.

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
