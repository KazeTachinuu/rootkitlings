# Hiding

Become invisible. Each ling hides one thing and shows how a defender detects it.
Pick only the hiding you need: module hiding alone may be enough, or stack several layers.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 34 | Module hiding | `list_del` on `THIS_MODULE->list`. | Hidden from `lsmod` but still in `/sys/module` and kernel memory. |
| 35 | Sysfs cleanup | Remove `/sys/module` entry after hiding from list. | Module memory still mapped. `crash` or `/proc/kallsyms` reveals it. |
| 36 | Getdents64 | Hook `getdents64` to filter directory entries by prefix. | Hidden files still accessible by exact path. `stat()` still works. |
| 37 | File content | Hook `read` to strip injected lines from file reads. | Byte offsets shift. A careful `dd` with seek reveals inconsistency. |
| 38 | Log hiding | Hook `do_syslog`/`devkmsg_read` to filter dmesg traces. | Cross-reference `dmesg` vs `/dev/kmsg` vs `journalctl`. `crash` reads the ring buffer directly. |
| 39 | Process hiding | Filter `/proc/[pid]` from getdents. | Process still in `/proc/sched_debug` and cgroup accounting. |
| 40 | Port hiding | Filter `/proc/net/tcp` reads, then hook `inet_diag`. | `ss` uses netlink, not procfs. First hook is bypassed. Second hook falls to raw `tcpdump`. |

A rootkit's "cardboard box" requires hiding a directory, hiding added file content,
and hiding the module from lsmod. Each ling here covers one of those.

Ling 36 (log hiding) is optional but closes a gap: module loading always writes to dmesg.
If your rootkit uses lings 32-35, the dmesg trace still remains.
Ling 36 hooks `do_syslog()` via ftrace (ling 30) to filter `printk` evidence of loading.

Every hiding technique has a bypass. Stack what your threat model requires.
