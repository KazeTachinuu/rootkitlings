# Hooking

Observe kernel control flow, then manage callback lifetime correctly. These
lessons target the course's x86-64 Linux 7.2 VM; syscall wrapper names and
register conventions are architecture-specific.

| # | Ling | One concept |
|--:|------|-------------|
| 29 | Debugfs | Create a diagnostic directory and file in debugfs. |
| 30 | Kprobe | Run a bounded callback at a function instruction. |
| 31 | Kretprobe | Observe a function return value with per-instance state. |
| 32 | Write protection | Demonstrate why patching protected kernel memory is brittle. |
| 33 | Fprobe | Trace a function by name through the ftrace-backed probe API. |
| 34 | rmdir trigger | Read syscall arguments safely in a kprobe callback. |
| 35 | Hook lifecycle | Register several callbacks and unwind partial initialization. |

The supported tracing APIs are the default teaching tools. Ling 32 is the
deliberate exception: it demonstrates an x86-64 page-table modification because
real rootkits patch protected memory. It is not presented as a portable driver
technique or as a foundation for later lessons.

The essential ownership rule is simple: once a callback is registered, its code
and state must remain alive until unregistering it has completed. Module exit
therefore unregisters hooks before releasing anything their callbacks touch.

After ling 35, build [Capstone 2: Tripwire](../../capstones/2-tripwire/README.md).
