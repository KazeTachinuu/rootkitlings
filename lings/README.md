# Rootkitlings roadmap

A Linux kernel rootkit combines many mechanisms.
Rootkitlings teaches each one separately, shows its weakness, then hardens it.

## Categories

| Category | Lings | What it covers |
|----------|------:|---------------|
| [Foundation](00_foundation/README.md) | 00-10 | Module lifecycle, build, multi-file, debugging, params, alloc, procfs |
| [Interfaces](01_interfaces/README.md) | 11-15 | Error pointers, proc write, lists, char device, ioctl |
| [Concurrency](02_concurrency/README.md) | 16-22 | Timer, workqueue, kthread, waiting, mutex, atomics |
| [Introspection](03_introspection/README.md) | 23-27 | Task, credentials, escalation, symbols, randomness |
| [Hooking](04_hooking/README.md) | 28-33 | Tracefs, kprobe, kretprobe, write protection, ftrace, lifecycle |
| [Hiding](05_hiding/README.md) | 34-40 | Module, sysfs, getdents64, file content, log, process, port |
| [Networking](06_networking/README.md) | 41-49 | Netfilter, packet sniffing, ICMP trigger, raw socket, DNS spoof, TCP backdoor, covert channel, traffic counter, packet mangling |
| [Persistence](07_persistence/README.md) | 50-54 | Proc trigger, init hooks, file infection, boot persistence, self-healing |
| [Crypto](08_crypto/README.md) | 55-57 | XOR obfuscation, AES encryption, hash verification |

58 lings. 6 [capstones](../capstones/README.md) spaced every ~10 lings where you
stop and build something real. Every kernel mechanism needed for a complete
rootkit.

## Teaching pattern

Each ling is a standalone concept card:

1. Build the minimal working version.
2. See what a defender sees.
3. Understand the tradeoff: what you gain, what you expose.

Pick the concepts your rootkit needs. Foundation (00-10) teaches kernel
module basics and is sequential. Everything after that is modular: hiding
works without crypto, persistence works without networking, jitter is
optional if you accept the detection risk.

## Rootkits you will meet

These lings reference real rootkits by name. Here is what they are:

| Rootkit | What it does | Source |
|---------|-------------|--------|
| **Diamorphine** | The textbook LKM rootkit. Hides modules, hides files, gives root via `kill -63`. Runs on kernels 2.6 through 6.x. Almost every technique in this course traces back to Diamorphine. | [github.com/m0nad/Diamorphine](https://github.com/m0nad/Diamorphine) |
| **Reptile** | Full-featured rootkit with encrypted C2, port knocking, and covert ICMP channels. Adds networking and crypto on top of Diamorphine's hiding tricks. | [github.com/f0rb1dd3n/Reptile](https://github.com/f0rb1dd3n/Reptile) |
| **KoviD** | Modern stealth rootkit. Uses ftrace hooks, hides from `/proc/modules` and sysfs, backdoors credentials, and supports multiple persistence methods. | [github.com/carloslack/KoViD](https://github.com/carloslack/KoViD) |
| **PUMAKIT** | Discovered in 2024. Uses ftrace to hook syscalls and `devkmsg_read` to filter its own kernel log messages. Demonstrates the current state of the art. | [elastic.co/security-labs/declawing-pumakit](https://elastic.co/security-labs/declawing-pumakit) |

You do not need to read their source code to complete the lings. They are referenced so you can see how a real rootkit applies each technique.

## Dependencies

Most categories are independent. The few real prerequisites:

| Category | Requires |
|----------|----------|
| Foundation (00-10) | Nothing. Start here. Sequential within itself. |
| Interfaces (11-15) | Foundation |
| Concurrency (16-22) | Foundation |
| Introspection (23-27) | Foundation |
| Hooking (28-33) | Foundation |
| Hiding (34-40) | Hooking (ftrace) |
| Networking (41-49) | Concurrency (kthread, mutex) |
| Persistence (50-54) | Foundation |
| Crypto (55-57) | Foundation |

Everything else is pick-and-choose. Each ling teaches one concept;
your final rootkit combines only the ones you choose.

## Paths

Three routes through the catalog. Start with the simplest, add what you need.

### Minimal rootkit (5 lings + 2 capstones)

A Diamorphine-style rootkit: loads, hides itself, gives root on a magic signal.

```
00 module -> 01 build -> ... -> 10 proc read
                                 |
                            CAPSTONE 0  message board
                                 |
11 interfaces -> ... -> 15 ioctl
                         |
                    CAPSTONE 1  command box
                         |
16 concurrency -> ... -> 27 randomness -> 28 tracefs -> ... -> 33 hook lifecycle
                                                                 |
                                                            CAPSTONE 2  tripwire
                                                                 |
                                          34 module hiding -> 25 credential escalation
```

Result: `insmod hello.ko`, module disappears from `lsmod`, `kill -63 <pid>` gives root.
Five core concepts, three capstones to practice along the way. Everything below is optional.

### Full rootkit (+14 lings + 1 capstone)

Adds networking, persistence, and crypto for a production-grade implant.

```
minimal path
  + 07 allocation, 08 rollback           (memory safety for long-running module)
  + 18 kthread, 21 mutex                 (background work)
  + 36 getdents64                        (hide files on disk)
  + 41 netfilter, 42 sniffer, 44 raw socket (network visibility)
  + 45 dns-spoof, 46 tcp-backdoor         (C2 loop)
  + 51 init-hooks, 52 file-infection       (survive reboot, run commands)
  + 55 xor-obfuscation, 56 aes-encryption  (encrypt traffic)
                                           |
                                      CAPSTONE 4  remote shell
```

Result: a rootkit that phones home, executes commands, transfers files,
hides itself, survives reboot, and encrypts its traffic.

### Full stealth (+11 lings + 2 capstones)

Hardens every surface. Pick any subset.

```
full rootkit path
  + 35 sysfs cleanup                    (hide /sys/module entry)
  + 37 file content, 38 log hiding      (filter reads, scrub dmesg)
  + 39 process, 40 port hiding          (hide from ps, ss)
                                          |
                                     CAPSTONE 3  dead drop
                                          |
  + 47 covert-channel, 48 traffic-counter (evade traffic analysis)
  + 49 packet-mangling                    (MITM redirection)
  + 57 hash-verification                  (replay protection)
  + 53 boot-persistence, 54 self-healing  (self-destruct, safe unload)
                                          |
                                     CAPSTONE 5  red vs blue
```

Each addition makes one detection vector harder. None is required for the rootkit to function.

## Rootkit capabilities

| Capability | Categories | Key lings |
|------------|------------|-----------|
| C2 connection | Networking + Concurrency | 41 netfilter-hook, 45 dns-spoof, 46 tcp-backdoor, 18 kthread |
| Boot persistence | Persistence | 53 boot-persistence |
| Authentication | Crypto | 55 xor-obfuscation |
| Command execution | Persistence | 51 init-hooks |
| File transfer | Networking + Persistence | 42 packet-sniffer, 50 proc-trigger |
| Stealth | Hiding | 34 module, 36 getdents64, 37 file-content, 38 log-hiding |
| Encrypted traffic | Crypto | 56 aes-encryption, 57 hash-verification |

## Research basis

Course design informed by analysis of:
- **C2 frameworks**: Cobalt Strike, Sliver, Havoc, Mythic, AdaptixC2
- **Real-world rootkits**: Diamorphine, Reptile, Drovorub (GRU), PUMAKIT, KoviD, Singularity, BPFDoor, Syslogk, VoidLink, RingReaper

## Upstream references

- [Linux Kernel Module Programming Guide](https://github.com/sysprog21/lkmpg) (community-maintained, covers module basics through chardev)
- [External modules](https://docs.kernel.org/kbuild/modules.html)
- [Module parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html)
- [Memory allocation](https://docs.kernel.org/core-api/mm-api.html)
- [Procfs](https://docs.kernel.org/filesystems/proc.html)
- [Lock types](https://docs.kernel.org/locking/locktypes.html)
- [Kprobes](https://docs.kernel.org/trace/kprobes.html)
- [Ftrace callbacks](https://docs.kernel.org/trace/ftrace-uses.html)
- [Netfilter hooks](https://docs.kernel.org/networking/netfilter.html)
- [Kernel crypto API](https://docs.kernel.org/crypto/api-aead.html)
