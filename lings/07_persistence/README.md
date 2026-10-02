# Persistence

Survive a reboot. Execute commands. Transfer files. Repair damage automatically.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 50 | Proc trigger | Writable `/proc` entry accepts a magic word to activate features. | The proc entry is visible. Defenders grep `/proc` for unexpected writable entries. |
| 51 | Usermode helper | `call_usermodehelper` spawns root processes from kernel space. | Spawned process is visible in `ps`. Arguments visible in `/proc/pid/cmdline`. |
| 52 | File infection | `filp_open`, `kernel_read`, `kernel_write` modify files from kernel. | File operations from kernel context bypass permission checks silently. Modified files leave filesystem journal traces. |
| 53 | Boot persistence | `modules-load.d` config written from kernel space for auto-load at boot. | Persistence file is on disk. Defenders check `/etc/modules-load.d/` and `modprobe.d install`. |
| 54 | Self-healing | Watchdog timer detects and repairs removed proc entries, killed threads. | Watchdog itself is visible. Crash during repair leaves partial state. |

Ling 50 is where rootkitlings meets real-world persistence. The operator
needs a way to activate features (escalate privileges, toggle hiding) without
leaving shell history. A writable proc entry with a magic word is the simplest trigger.

Ling 53 teaches three boot persistence methods in order of stealth:

1. `/etc/modules-load.d/hello.conf` (obvious, easy to find)
2. `modprobe.d` install directive (piggybacks on a legitimate module)
3. DKMS (survives kernel upgrades)

If your rootkit needs to hide the persistence files, ling 36 (getdents64 hook) covers that.
Cross-category combinations like this are where the modular design pays off.

After ling 54: build [Capstone 5: Red vs blue](../../capstones/5-red-vs-blue/README.md) (together with crypto).
