# 07 - Allocation, ownership, and lifetime
## Why this step

Our callbacks have used variables whose storage was created with the module.
Later interfaces need objects created at runtime and kept after init returns.
That introduces two questions: who owns the object, and when does it stop
existing?

Init allocates one object; exit releases it.

## Simplify the previous module

Keep the module includes, init function, exit function, registration macros,
license, and description. Remove the `name` parameter and its callback from
lings 05-06; this lesson replaces that experiment with owned heap state.

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

In your init function, allocate the struct with Linux 7.2's typed allocator:

```c
state = kmalloc_obj(*state, GFP_KERNEL);
```

`kmalloc_obj` derives the allocation size from the object type, so the size
cannot drift when the pointer type changes. It returns a pointer to kernel heap
memory, or `NULL` on failure.
`GFP_KERNEL` permits sleeping (safe in module init, which runs in process
context).

If `kmalloc_obj` returns `NULL`, init must return `-ENOMEM` (from ling 04,
you know a negative return rejects the load). Otherwise, set the value
to 42 and log it.

In your exit function, free with `kfree(state)` and set the pointer to `NULL`.
Log that state was freed.

## Map the lifetimes

```
         insmod              loaded              rmmod
           |                   |                   |
           v                   v                   v
  init: kmalloc_obj ------> return 0            exit:
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
  *state         |=========================|              heap (allocate..free)
  locals   |===|                                          stack (one call)
  code     |=========|=========|=================|        module memory
                 init    loaded         exit
```

| Item | Storage | Lifetime |
|---|---|---|
| `state` pointer | Static | Loaded module |
| `*state` object | Kernel heap | `kmalloc_obj` to `kfree` |
| Local variables | Init stack | One function call |
| Init function code | Module memory | Load to unload |

## Contract

The host check verifies that `hello.ko` links against both allocation and free
symbols. The guest check loads, unloads, and repeats the cycle without a kernel
warning.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
dmesg | tail
rmmod hello
dmesg | tail
```

## Verify

```bash
./rootkitlings check 7
```

Reload catches stale module state and incomplete teardown. The linked symbols
prove that both operations exist, not that every control-flow path pairs them;
that stronger question needs review or instrumentation such as kmemleak.
> **What a defender sees**: if init returns an error after allocation, exit never runs.
> The allocated memory leaks permanently. The kernel has no garbage collector.

See also: [08 - Rollback](../08-rollback/README.md).
