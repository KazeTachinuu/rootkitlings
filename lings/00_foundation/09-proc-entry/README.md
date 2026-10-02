# 09 - Publish a procfs entry
## Why this step

We can now acquire resources and release them on every path, but userspace has
no custom interface to our module. We first need to publish a path and remove it
before the module disappears.

```
Procfs entry lifecycle

  insmod                                       rmmod
    |                                            |
    v                                            v
  proc_create("rk_entry", 0444, NULL, &rk_ops)      proc_remove(entry)
    |                                            |
    v                                            v
  /proc/rk_entry exists                           /proc/rk_entry gone
  ls: visible                                  ls: not found
  cat: calls rk_ops callbacks                  module memory freed
```

## Reset the experiment

Inspect the rollback experiment, then restore the minimal module:

```bash
git diff -- module/hello.c
git restore module/hello.c
```

Then add only the procfs pieces below to
[hello.c](../../../module/hello.c).

## Describe an interface

Add:

```c
#include <linux/errno.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *entry;

static const struct proc_ops rk_ops = {
};
```

`struct proc_ops` is an operations table: callback addresses that procfs
invokes on file operations. Empty for now; ling 10 adds the read callback.

```
struct proc_ops rk_ops
+------------------+------------------+
| .proc_read       |      NULL        |  <- no callback yet
| .proc_write      |      NULL        |
| .proc_open       |      NULL        |
+------------------+------------------+

Procfs holds &rk_ops. Any file operation on /proc/rk_entry
follows these pointers. NULL means the operation fails.
```

## Publish during init

Replace init with:

```c
static int __init entry_init(void)
{
	entry = proc_create("rk_entry", 0444, NULL, &rk_ops);
	if (!entry)
		return -ENOMEM;

	return 0;
}
```

The arguments mean:

| Argument | Meaning |
|---|---|
| `"rk_entry"` | Entry name |
| `0444` | Read-only permissions |
| `NULL` | Parent is the root of `/proc` |
| `&rk_ops` | Address of the operations table |

Replace exit with:

```c
static void __exit entry_exit(void)
{
	proc_remove(entry);
}
```

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
ls /proc/rk_entry
insmod hello.ko
ls -l /proc/rk_entry
cat /proc/rk_entry
echo "status=$?"
rmmod hello
ls /proc/rk_entry
dmesg | tail
```

## Contract

The check script verifies:

- `/proc/rk_entry` does not exist before loading.
- After `insmod hello.ko`: `/proc/rk_entry` exists.
- After `rmmod hello`: `/proc/rk_entry` is gone.

## Verify

```bash
./rootkitlings check 7
```
> **What a defender sees**: `/proc/rk_entry` is visible to every user with `ls /proc`.
> It has no read operation, so it's useless. And its mere presence is suspicious.

See also: [08 - Procfs read](../08-proc-read/README.md).
