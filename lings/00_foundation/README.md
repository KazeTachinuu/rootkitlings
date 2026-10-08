# Foundation

Turn `hello.c` into a loadable module with correct lifecycle and ownership.

The source moves from one lifecycle operation to explicit ownership and rollback.

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
Continue with [Procfs](../01_procfs/README.md).
