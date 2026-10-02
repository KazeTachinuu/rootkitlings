# Rootkitlings roadmap

A Linux kernel rootkit combines many mechanisms.
Rootkitlings teaches each one separately, shows its weakness, then hardens it.

## Categories

| Category | Lings | What it covers |
|----------|------:|---------------|
| [Foundation](00_foundation/README.md) | 00-08 | Module lifecycle, build, params, alloc, procfs |
| [Interfaces](01_interfaces/README.md) | 09-13 | Error pointers, proc write, lists, char device, ioctl |
| [Concurrency](02_concurrency/README.md) | 14-20 | Timer, workqueue, kthread, waiting, mutex, atomics |
| [Introspection](03_introspection/README.md) | 21-25 | Task, credentials, escalation, symbols, randomness |
| [Hooking](04_hooking/README.md) | 26-31 | Tracefs, kprobe, kretprobe, write protection, ftrace, lifecycle |
| [Hiding](05_hiding/README.md) | 32-38 | Module, sysfs, getdents64, file content, log, process, port |
| [Networking](06_networking/README.md) | 39-47 | Netfilter, packet sniffing, ICMP trigger, raw socket, DNS spoof, TCP backdoor, covert channel, traffic counter, packet mangling |
| [Persistence](07_persistence/README.md) | 48-52 | Proc trigger, init hooks, file infection, boot persistence, self-healing |
| [Crypto](08_crypto/README.md) | 53-55 | XOR obfuscation, AES encryption, hash verification |

56 lings. 6 [capstones](../capstones/README.md) spaced every ~10 lings where you
stop and build something real. Every kernel mechanism needed for a complete
rootkit.

## Teaching pattern

Each ling is a standalone concept card:

1. Build the minimal working version.
2. See what a defender sees.
3. Understand the tradeoff: what you gain, what you expose.

Pick the concepts your rootkit needs. Foundation (00-08) teaches kernel
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
| Foundation (00-08) | Nothing. Start here. Sequential within itself. |
| Interfaces (09-13) | Foundation |
| Concurrency (14-20) | Foundation |
| Introspection (21-25) | Foundation |
| Hooking (26-31) | Foundation |
| Hiding (32-38) | Hooking (ftrace) |
| Networking (39-47) | Concurrency (kthread, mutex) |
| Persistence (48-52) | Foundation |
| Crypto (53-55) | Foundation |

Everything else is pick-and-choose. Each ling teaches one concept;
your final rootkit combines only the ones you choose.

## Paths

Three routes through the catalog. Start with the simplest, add what you need.

### Minimal rootkit (5 lings + 2 capstones)

A Diamorphine-style rootkit: loads, hides itself, gives root on a magic signal.

```
00 module -> 01 build -> ... -> 08 proc read
                                 |
                            CAPSTONE 0  message board
                                 |
09 interfaces -> ... -> 13 ioctl
                         |
                    CAPSTONE 1  command box
                         |
14 concurrency -> ... -> 25 randomness -> 26 tracefs -> ... -> 31 hook lifecycle
                                                                 |
                                                            CAPSTONE 2  tripwire
                                                                 |
                                          32 module hiding -> 23 credential escalation
```

Result: `insmod hello.ko`, module disappears from `lsmod`, `kill -63 <pid>` gives root.
Five core concepts, three capstones to practice along the way. Everything below is optional.

### Full rootkit (+14 lings + 1 capstone)

Adds networking, persistence, and crypto for a production-grade implant.

```
minimal path
  + 05 allocation, 06 rollback           (memory safety for long-running module)
  + 16 kthread, 19 mutex                 (background work)
  + 34 getdents64                        (hide files on disk)
  + 39 netfilter, 40 sniffer, 42 raw socket (network visibility)
  + 43 dns-spoof, 44 tcp-backdoor         (C2 loop)
  + 49 init-hooks, 50 file-infection       (survive reboot, run commands)
  + 53 xor-obfuscation, 54 aes-encryption  (encrypt traffic)
                                           |
                                      CAPSTONE 4  remote shell
```

Result: a rootkit that phones home, executes commands, transfers files,
hides itself, survives reboot, and encrypts its traffic.

### Full stealth (+11 lings + 2 capstones)

Hardens every surface. Pick any subset.

```
full rootkit path
  + 33 sysfs cleanup                    (hide /sys/module entry)
  + 35 file content, 36 log hiding      (filter reads, scrub dmesg)
  + 37 process, 38 port hiding          (hide from ps, ss)
                                          |
                                     CAPSTONE 3  dead drop
                                          |
  + 45 covert-channel, 46 traffic-counter (evade traffic analysis)
  + 47 packet-mangling                    (MITM redirection)
  + 55 hash-verification                  (replay protection)
  + 51 boot-persistence, 52 self-healing  (self-destruct, safe unload)
                                          |
                                     CAPSTONE 5  red vs blue
```

Each addition makes one detection vector harder. None is required for the rootkit to function.

## Rootkit capabilities

| Capability | Categories | Key lings |
|------------|------------|-----------|
| C2 connection | Networking + Concurrency | 39 netfilter-hook, 43 dns-spoof, 44 tcp-backdoor, 16 kthread |
| Boot persistence | Persistence | 51 boot-persistence |
| Authentication | Crypto | 53 xor-obfuscation |
| Command execution | Persistence | 49 init-hooks |
| File transfer | Networking + Persistence | 40 packet-sniffer, 48 proc-trigger |
| Stealth | Hiding | 32 module, 34 getdents64, 35 file-content, 36 log-hiding |
| Encrypted traffic | Crypto | 54 aes-encryption, 55 hash-verification |

## Research basis

Course design informed by analysis of:
- **C2 frameworks**: Cobalt Strike, Sliver, Havoc, Mythic, AdaptixC2
- **Real-world rootkits**: Diamorphine, Reptile, Drovorub (GRU), PUMAKIT, KoviD, Singularity, BPFDoor, Syslogk, VoidLink, RingReaper

## Upstream references

- [External modules](https://docs.kernel.org/kbuild/modules.html)
- [Module parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html)
- [Memory allocation](https://docs.kernel.org/core-api/mm-api.html)
- [Procfs](https://docs.kernel.org/filesystems/proc.html)
- [Lock types](https://docs.kernel.org/locking/locktypes.html)
- [Kprobes](https://docs.kernel.org/trace/kprobes.html)
- [Ftrace callbacks](https://docs.kernel.org/trace/ftrace-uses.html)
- [Netfilter hooks](https://docs.kernel.org/networking/netfilter.html)
- [Kernel crypto API](https://docs.kernel.org/crypto/api-aead.html)
