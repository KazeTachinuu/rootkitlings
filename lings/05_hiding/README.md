# Hiding

Change one observable view at a time, then inspect the views that still expose
the object. These exercises demonstrate filtering, not invisibility.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 36 | Module hiding | `list_del` on `THIS_MODULE->list`. | Hidden from `lsmod` but still in `/sys/module` and kernel memory. |
| 37 | Sysfs hiding | Remove the `/sys/module` view after list hiding. | Module memory and other references remain. |
| 38 | Getdents64 | Hook `getdents64` to filter directory entries by prefix. | Hidden files still accessible by exact path. `stat()` still works. |
| 39 | File content | Hook `read` to strip injected lines from file reads. | Byte offsets shift. A careful `dd` with seek reveals inconsistency. |
| 40 | Log hiding | Hook `do_syslog`/`devkmsg_read` to filter dmesg traces. | Cross-reference `dmesg` vs `/dev/kmsg` vs `journalctl`. `crash` reads the ring buffer directly. |
| 41 | Process hiding | Filter `/proc/[pid]` from getdents. | Process still in `/proc/sched_debug` and cgroup accounting. |
| 42 | Port hiding | Filter `/proc/net/tcp` reads, then hook `inet_diag`. | `ss` uses netlink, not procfs. First hook is bypassed. Second hook falls to raw `tcpdump`. |

The exercises alter directory, file-content, module-list, log, process, and
socket views independently. No combination demonstrated here removes every
observation path.

Ling 40 filters selected records returned through the syslog path. Other log
readers and external collectors can retain the original records.
Ling 40 uses a kretprobe to filter `do_syslog()` output before it reaches
userspace.

Every hiding technique has a bypass. Stack what your threat model requires.

After ling 42: build [Capstone 3: Dead drop](../../capstones/3-dead-drop/README.md).
