# Persistence

Survive a reboot. Execute commands. Transfer files. Clean up on command.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 50 | Kernel file I/O | `filp_open`, `kernel_read`, `kernel_write`. | File operations from kernel context bypass permission checks silently. |
| 51 | Command execution | `call_usermodehelper` with stdout/stderr capture. | Spawned process is visible in `ps`. Arguments visible in `/proc/pid/cmdline`. |
| 52 | Boot persistence | `modules-load.d`, `modprobe.d` install hijacking, DKMS. | Persistence file is on disk. Defenders check `/etc/modules-load.d/`, `modprobe.d install`, `dkms status`. |
| 53 | Kill date | Timer self-destruct + dead-man switch on lost C2 contact. | Crash during self-destruct leaves dump. Filesystem journal reveals overwritten files. |
| 54 | Clean exit | Undo all hooks, free memory, remove persistence, unload. | A crash during cleanup leaves partial state. Rollback order matters. |

A production rootkit needs persistence, command execution, and file transfer.

Ling 50 is where rootkitlings meets real-world persistence. It teaches three methods
in order of stealth:

1. `/etc/modules-load.d/hello.conf` (obvious, easy to find)
2. `modprobe.d` install directive (piggybacks on a legitimate module)
3. DKMS (survives kernel upgrades)

If your rootkit needs to hide the persistence files, ling 34 (getdents64 hook) covers that.
Cross-category combinations like this are where the modular design pays off.

Clean exit (ling 52) undoes whatever your rootkit uses, in reverse order.
Only the mechanisms you included need cleanup. One forgotten hook, one leaked
timer, one uncancelled workqueue item, and the kernel crashes on unload.
