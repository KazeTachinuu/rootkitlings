# 09 - Error pointers encode failure in a return value

## Why this step

`kmalloc` returns `NULL` on failure, and that is the only signal.
Other kernel functions return `ERR_PTR(-ECODE)`: a pointer that encodes
an error in the last page of virtual memory. Dereferencing it crashes
exactly like dereferencing `NULL`, but it carries the reason.

Ling 07 used `proc_create`, which returns `NULL` on failure.
This ling introduces `proc_mkdir` and teaches the three-way distinction
every kernel caller must know: `NULL`, `ERR_PTR`, or valid pointer.

## The trap

`proc_create` returns `NULL` on failure.
`proc_mkdir` also returns `NULL`.
But many filesystem-level functions return `ERR_PTR`.
Mixing them up crashes the kernel.

```
  Pointer value                  Meaning
  ---------------------------    ----------------------------
  NULL (0)                       Allocation failed, no reason
  0x1 .. 0xFFFF_EFFF            Valid pointer
  0xFFFF_F000 .. 0xFFFF_FFFF    ERR_PTR(-1 .. -4095)
```

## The ERR_PTR API

Three macros from `linux/err.h` work together:

```c
void *ERR_PTR(long error);     // encode: errno -> pointer
long  PTR_ERR(const void *p);  // decode: pointer -> errno
bool  IS_ERR(const void *p);   // test: is this an error pointer?
```

```
  IS_ERR(NULL)              -> false  (NULL is not ERR_PTR)
  IS_ERR(ERR_PTR(-ENOMEM)) -> true
  PTR_ERR(ERR_PTR(-ENOMEM)) -> -ENOMEM
```

A caller that only checks `IS_ERR` will miss a `NULL` failure.
A caller that only checks `NULL` will dereference an `ERR_PTR` and crash.
The safe pattern checks both.

## Reset the experiment

```bash
git diff -- module/hello.c
git restore module/hello.c
```

## What to build

Create a `/proc/rk_errptr/` directory with a `status` file inside it,
plus a demonstration that ERR_PTR encoding works.

```
  /proc/
    rk_errptr/            <- proc_mkdir("rk_errptr", NULL)
      status          <- proc_create("status", 0444, dir, &ops)

  Init also calls a demo function that returns
  either a valid kmalloc pointer or ERR_PTR(-ENOMEM),
  depending on a bool argument.
```

You need these includes (beyond the base module):

```c
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
```

## Key concepts

**proc_mkdir** creates a directory under `/proc`. It returns `NULL`
on failure (same as `proc_create`). You need two `proc_dir_entry`
pointers: one for the directory, one for the status file inside it.

**Nested proc_create**: pass the directory entry as the third argument
to `proc_create` instead of `NULL`. This places the file inside the
directory.

**The read callback** for `/proc/rk_errptr/status` follows the exact
same `simple_read_from_buffer` pattern you wrote in ling 08. It
should return `ok\n`.

**The demo function** takes a `bool fail` parameter:
- If true, return `ERR_PTR(-ENOMEM)` (simulate failure).
- If false, `kmalloc(16, GFP_KERNEL)` and return the result.

The caller must handle three cases:

```
  demo_errptr(fail)
       |
  fail = true?
      / \
    yes   no
     |     |
     v     v
  ERR_PTR  kmalloc(16)
  (-ENOMEM)   |
     |     +--+--+
     |     |     |
     |   NULL   valid
     |     |     |
     v     v     v
  IS_ERR  NULL   use it
  = true  check  then kfree
```

**Rollback in init**: you already practiced this in ling 06. If
`proc_create` fails after `proc_mkdir` succeeded, remove the
directory before returning an error.

**Exit cleanup**: remove child before parent (`proc_remove` each in
reverse order).

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko
ls /proc/rk_errptr/
cat /proc/rk_errptr/status
dmesg | tail
rmmod hello
ls /proc/rk_errptr
```

## Contract

The check script verifies:

- `/proc/rk_errptr` is a directory (created with `proc_mkdir`).
- `/proc/rk_errptr/status` exists and returns non-empty content.
- After unload, both `/proc/rk_errptr/status` and `/proc/rk_errptr` are gone.

## Verify

```bash
./rootkitlings check 9
```
> **What a defender sees**: `/proc/rk_errptr/` is a visible directory.
> `ls /proc/` shows it. A subdirectory makes the module's presence
> more obvious than a single flat entry.

See also: [10 - Procfs write](../10-proc-write/README.md).
