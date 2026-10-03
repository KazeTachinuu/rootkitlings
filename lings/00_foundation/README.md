# Foundation

Turn `hello.c` into a loadable module that safely serves `/proc/rk_entry`.

The source evolves across lings. When a lesson replaces the previous
experiment, it names the exact declarations to remove; no Git knowledge or
destructive reset command is required.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 00 | Kernel module | Load and unload kernel code. | Module is visible in `lsmod`. |
| 01 | Build | Produce and identify `hello.ko`. | `file` and `modinfo` reveal everything about it. |
| 02 | Multi-file | Split a module across multiple source files and a shared header. | Build structure is invisible at runtime, but aids organization. |
| 03 | GDB debug | Attach GDB to the QEMU VM, set breakpoints, inspect state. | Debug sessions are local. No runtime artifact to detect. |
| 04 | Init failure | Return an error from init. | A failed load still leaves a dmesg trace. |
| 05 | Parameter | Expose state through sysfs. | Parameters are world-readable by default. |
| 06 | Callback | React to a parameter write. | Anyone with write access can trigger the callback. |
| 07 | Allocation | Own heap memory across callbacks. | Forgetting to free leaks memory permanently. |
| 08 | Rollback | Undo partial init with goto. | Without rollback, a half-init leaves dangling resources. |
| 09 | Procfs entry | Publish `/proc/rk_entry`. | The entry is visible to every user on the system. |
| 10 | Procfs read | Return bounded data to userspace. | Without bounds, read overflows into adjacent memory. |

At ling 10: the entry exists while loaded, dispatches reads, reaches EOF, disappears on unload.
Foundation is strictly sequential. The numbered route remains the clearest path
through later categories because each one reuses earlier vocabulary.

After ling 10: continue with [Interfaces](../01_interfaces/README.md).
