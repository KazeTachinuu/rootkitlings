# Foundation

Turn `hello.c` into a loadable module that safely serves `/proc/rk_entry`.

The source evolves across lings.
After each group, we reset the module so one mechanism remains visible.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 00 | Kernel module | Load and unload kernel code. | Module is visible in `lsmod`. |
| 01 | Build | Produce and identify `hello.ko`. | `file` and `modinfo` reveal everything about it. |
| 02 | Init failure | Return an error from init. | A failed load still leaves a dmesg trace. |
| 03 | Parameter | Expose state through sysfs. | Parameters are world-readable by default. |
| 04 | Callback | React to a parameter write. | Anyone with write access can trigger the callback. |
| 05 | Allocation | Own heap memory across callbacks. | Forgetting to free leaks memory permanently. |
| 06 | Rollback | Undo partial init with goto. | Without rollback, a half-init leaves dangling resources. |
| 07 | Procfs entry | Publish `/proc/rk_entry`. | The entry is visible to every user on the system. |
| 08 | Procfs read | Return bounded data to userspace. | Without bounds, read overflows into adjacent memory. |

At ling 08: the entry exists while loaded, dispatches reads, reaches EOF, disappears on unload.
Foundation is the only sequential category.

After ling 08: build [Capstone 0: Message board](../../capstones/0-message-board/README.md),
then pick any category that interests you.
