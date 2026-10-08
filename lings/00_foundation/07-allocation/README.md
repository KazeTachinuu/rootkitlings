# 07 - Own kernel heap memory

State created during init can outlive the init call and remain available until module exit.

This module allocates one object, stores `42` in it, and releases it on unload.

## Observe it

```sh
insmod hello.ko
dmesg | tail
rmmod hello
dmesg | tail
```

```text
rk: allocated value=42
rk: freed state
```

Loading and unloading a second time must produce the same clean lifecycle.

## Ownership

```text
module init                 module exit
     |                           |
     v                           v
kmalloc_obj() -> state -----> kfree(state)
                  |
                  v
              value = 42
```

`state` is a static pointer stored with the loaded module.

`*state` is a separate object on the kernel heap and exists only between successful allocation and `kfree()`.

`kmalloc_obj(*state, GFP_KERNEL)` derives the allocation size from the pointed-to type.

`GFP_KERNEL` may sleep, so it is suitable in module init but not in interrupt, timer, or probe context.

A failed allocation returns `NULL`.

Init must return `-ENOMEM` instead of dereferencing it.

## Implement it

Remove the parameter callback state from the current module and add one `struct implant_state` containing an integer value.

Keep one static pointer named `state`.

In init, allocate `*state` with `kmalloc_obj()` and `GFP_KERNEL`, reject `NULL`, assign `42`, and log the stored value.

In exit, call `kfree(state)`, set the pointer to `NULL`, and log the release.

## Verify

```sh
./rootkitlings check L07
```

The host verification requires real allocation and release symbol dependencies in `hello.ko`.

The guest loads, unloads, reloads, and rejects kernel warnings.

Those checks prove both operations exist and complete in this path; proving the absence of every leak requires review or instrumentation such as kmemleak.

Next: [08 - Roll back partial initialization](../08-rollback/README.md).
