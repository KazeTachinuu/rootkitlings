# Give the module private state

The module now needs memory for information that must survive between calls.
Give that memory one clear owner and release it on every exit path.
Success proves that private state lives exactly as long as the module, including
when startup fails.

Kernel memory with this lifetime is the module's **private state**.
One component creates it, and the same component destroys it.

```text
insmod                                     rmmod
  |                                          |
  v                                          v
rk_init()                               rk_exit()
  |                                          |
  +-- rk_state_create()                      +-- rk_state_destroy()
  |     |                                    |     |
  |     +-- kzalloc() -> struct rk_state *   |     +-- kfree()
  |                                          |
  +-- (on failure: destroy + return error)   +-- state = NULL
```

Here, `core/state.c` owns a small heap object.
`core/main.c` controls when that object exists but cannot inspect its fields.
During init, `rk_state_create()` allocates it with `kzalloc()`.
During unload, `rk_state_destroy()` releases it with `kfree()`.

## One return value

`kzalloc(size, GFP_KERNEL)` returns zero-filled memory or `NULL`.
`GFP_KERNEL` tells the allocator it may sleep while finding memory, which is
allowed during module init but not inside interrupt handlers.

A plain `NULL` carries no reason for the failure.
The state constructor converts `NULL` to `ERR_PTR(-ENOMEM)`, encoding the exact
negative errno into the pointer itself.
The caller must use `IS_ERR()` before treating that value as an object and
`PTR_ERR()` to recover the original negative error.
An error pointer is encoded error information; it must never be dereferenced.

The header only forward-declares `struct rk_state`.
That makes the type **opaque**: other files may hold its pointer but only
`core/state.c` knows its layout or calls `kfree()` for it.
`__must_check` asks the compiler to warn when a caller ignores the constructor
result.

Failed module initialization does not call the exit callback.
If state creation fails, init must preserve the constructor's error and leave
the stored pointer as `NULL`.
Normal unload destroys the state and immediately sets that pointer to `NULL`, so
later code cannot accidentally reuse released memory.

## Your task

This adds `include/rootkit.h` and `core/state.c` to the existing module.
Edit all four declared files:

- Extend `Kbuild` to link `core/main.o` and `core/state.o`, and add the
  `include` directory to compiler search paths.
- In `include/rootkit.h`, define failure points 0 through 2, forward-declare
  `struct rk_state`, and declare the create/destroy API with `__must_check` on
  the constructor.
- In `core/state.c`, keep the structure definition private, allocate it with
  `kzalloc(..., GFP_KERNEL)`, and make this file its sole allocator and
  releaser.
- Make failure point 2 return `ERR_PTR(-ENOMEM)` through the same constructor
  path used for a real allocation failure.
- In `core/main.c`, use `IS_ERR()` and `PTR_ERR()`, preserve the exact error,
  and clear the stored pointer after a failed create and after destruction.
- Keep failure point 1 returning `-ECANCELED`, reject unknown build-time values,
  and preserve the module name and metadata.

Do not free state from `core/main.c`, expose the structure fields in the header,
or use a log message as proof of ownership.

## Check and observe

Run:

```sh
./rklings check
```

The checker builds failure points 0, 1, and 2 without warnings and rejects an
unknown value at compile time.
In disposable guests, value 1 must return numeric `ECANCELED`, value 2 must
return numeric `ENOMEM`, and both rejected loads must leave the module absent.
Value 0 must load, unload, disappear from kernel inventory and sysfs, then load
and unload again.

On the host, injected allocator and constructor results verify the exact object
lifetime and error propagation without relying on source-text patterns.
Object-file and header checks enforce only the component boundary: allocation
and release belong to `core/state.c`, and the state layout stays private.

## References

- [Linux memory allocation guide][ref-1]
- [Error pointers][ref-2]
- [Driver basics: module initialization and exit][ref-3]

[ref-1]:
  https://docs.kernel.org/core-api/memory-allocation.html

[ref-2]:
  https://docs.kernel.org/core-api/kernel-api.html#error-pointers

[ref-3]:
  https://docs.kernel.org/driver-api/basics.html#c.module_init
