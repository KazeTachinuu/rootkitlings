# Persistence

Survive a reboot. Transfer files. Repair damage automatically.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 52 | Proc trigger | Writable `/proc` entry accepts a magic word to activate features. | The proc entry is visible. Defenders grep `/proc` for unexpected writable entries. |
| 53 | Init hooks | Hook init scripts or systemd to auto-load the module at boot. | Persistence file is on disk. Defenders check `/etc/modules-load.d/` and `modprobe.d install`. |
| 54 | File infection | `filp_open`, `kernel_read`, `kernel_write` modify files from kernel. | File operations from kernel context bypass permission checks silently. Modified files leave filesystem journal traces. |
| 55 | Boot persistence | `modules-load.d` config written from kernel space for auto-load at boot. | Persistence file is on disk. Defenders check standard locations. |
| 56 | Self-healing | Watchdog timer detects and repairs removed proc entries, killed threads. | Watchdog itself is visible. Crash during repair leaves partial state. |

Ling 52 is where rootkitlings meets real-world persistence. The operator
needs a way to activate features (escalate privileges, toggle hiding) without
leaving shell history. A writable proc entry with a magic word is the simplest trigger.

Ling 55 teaches three boot persistence methods in order of stealth:

1. `/etc/modules-load.d/hello.conf` (obvious, easy to find)
2. `modprobe.d` install directive (piggybacks on a legitimate module)
3. DKMS (survives kernel upgrades)

If your rootkit needs to hide the persistence files, ling 38 (getdents64 hook) covers that.

After ling 56: continue with [Crypto](../08_crypto/README.md), then build
[Capstone 5: Red vs blue](../../capstones/5-red-vs-blue/README.md).
