# 07 - Allocation, ownership, and lifetime
## Why this step

Our callbacks have used variables whose storage was created with the module.
Later interfaces need objects created at runtime and kept after init returns.
That introduces two questions: who owns the object, and when does it stop
existing?

Init allocates one object; exit releases it.

## Reset the experiment

First inspect the experiment you are about to discard, then restore the
repository's minimal module:

```bash
git diff -- module/hello.c
git restore module/hello.c
```

`git restore` replaces the working copy of this one file with its committed
version. It deliberately discards the parameter edits from lings 05 and 06.

## Add one owned object

Add:

```c
#include <linux/errno.h>
#include <linux/slab.h>

struct rk_state {
	int value;
};

static struct rk_state *state;
```

This declares a structure type and a pointer. No object is allocated yet.

In your init function, allocate the struct with `kmalloc`. The pattern:

```c
state = kmalloc(sizeof(*state), GFP_KERNEL);
```

`kmalloc` returns a pointer to kernel heap memory, or `NULL` on failure.
`sizeof(*state)` follows the pointed-to type without repeating its name.
`GFP_KERNEL` permits sleeping (safe in module init, which runs in process
context).

If `kmalloc` returns `NULL`, init must return `-ENOMEM` (from ling 04,
you know a negative return rejects the load). Otherwise, set the value
to 42 and log it.

In your exit function, free with `kfree(state)` and set the pointer to `NULL`.
Log that state was freed.

## Map the lifetimes

```
         insmod              loaded              rmmod
           |                   |                   |
           v                   v                   v
  init:  kmalloc ---------> return 0            exit:
           |                   |                   |
           v                   |                kfree
        state ---> *state      |                   |
       (pointer)  (object)     |                state = NULL
           |         |         |                   |
           |    heap memory    |                   v
           |    [ value: 42 ]  |              pointer = NULL
           |         |         |              object freed
           v         v         v
 
  state    |=========|=========|=================|        static storage
  *state         |=========================|              heap (kmalloc..kfree)
  locals   |===|                                          stack (one call)
  code     |=========|=========|=================|        module memory
                 init    loaded         exit
```

| Item | Storage | Lifetime |
|---|---|---|
| `state` pointer | Static | Loaded module |
| `*state` object | Kernel heap | `kmalloc` to `kfree` |
| Local variables | Init stack | One function call |
| Init function code | Module memory | Load to unload |

## Contract

The check script verifies:

- `dmesg` contains `42` (the allocated value is logged).
- The module loads and unloads cleanly twice (no resource leak).

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko
dmesg | tail
rmmod hello
dmesg | tail
```

## Verify

```bash
./rootkitlings check 7
```

The check also reloads the module to verify no resource leak from the first run.
> **What a defender sees**: if init returns an error after `kmalloc`, exit never runs.
> The allocated memory leaks permanently. The kernel has no garbage collector.

See also: [08 - Rollback](../08-rollback/README.md).
