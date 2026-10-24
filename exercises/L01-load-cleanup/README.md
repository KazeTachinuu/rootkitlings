# L01: Clean Up a Failed Module Load

## Mission

A rootkit that crashes on partial init draws attention.
Silent failure is the baseline.

L00 loads a module that cannot fail.
Real initialization acquires multiple resources, and any step can fail after
earlier steps already committed.

Create two channels and make every failure path release only what was already
created, so a failed load never leaves pieces behind in the kernel.

## What you will observe

`ENOMEM` is the kernel's "out of memory" error.
A failure point is a supplied test setting that makes one chosen allocation
return that error.

After either rejected load, the module is absent from both `/sys/module` and
`/proc/modules`.
This is the proof that failed initialization left no loaded module behind.

The correct cleanup depends on how far initialization progressed:

```text
failure point       resources owned       required cleanup
-------------       ---------------       ----------------
before A            none                  return error
while creating A    none                  return error
while creating B    A                     free A, return error
no failure          A and B               keep both until module exit
```

Returning immediately after B fails would leak A.
Destroying B would be wrong: B was never created.
The table shows exactly what must be cleaned up at each point.

## Workspace

```text
exercises/L01-load-cleanup
├── core
│   └── main.c
└── Kbuild
```

Only `core/main.c` is editable.

Supplied code creates and destroys the two channels.
Each destruction logs the channel ID so you can see the cleanup happen.

## Understand kernel error pointers

`rk_channel_create` returns either a channel pointer or the exact reason it
failed.
Kernel code can store a small negative error number in a pointer value:

```text
successful result                     failed result

rk_channel_create()                   rk_channel_create()
        |                                      |
        v                                      v
 valid struct rk_channel *              ERR_PTR(-ENOMEM)
        |                                      |
   IS_ERR(ptr) == false                   IS_ERR(ptr) == true
                                               |
                                               v
                                        PTR_ERR(ptr)
                                               |
                                               v
                                           -ENOMEM
```

Kernel functions return the negative form, `-ENOMEM`, so `insmod` receives the
exact reason the load failed.

The three helpers have separate jobs:

```c
IS_ERR(pointer)   /* does this pointer contain an error? */
PTR_ERR(pointer)  /* recover its negative errno */
ERR_PTR(-ENOMEM)  /* store an errno in a pointer value */
```

Do not test an error pointer with only `if (!pointer)` because
`ERR_PTR(-ENOMEM)` is not `NULL`.

The supplied allocator injects `-ENOMEM` at channel A or B when validation
requests it.
Your initialization code handles those results exactly like a real allocation
failure; you do not implement or configure the injector.

A cleanup label names a later line that `goto` can jump to, letting several
failures share one cleanup path:

```c
if (operation_failed)
	goto cleanup;

cleanup:
	release_owned_resource();
	return error;
```

In this exercise, the jump is always downward and only releases work that has
already succeeded.

## TODOs

```text
TODO 1  core/main.c:49:2  create both channels or clean up the first
TODO 2  core/main.c:55:2  destroy both channels in reverse order
```

### TODO 1 -- `core/main.c:49:2` -- Complete initialization or undo it

```text
success  -> keep channels A and B, return 0
failure  -> keep no channels, return the exact error
```

Create channel A, then channel B.
If either creation returns an error pointer, extract its error with `PTR_ERR`,
clear the pointer, and ensure everything already created is destroyed before
returning.

Use a `goto` cleanup label so B's failure path destroys A without duplicating
the release code.
Log `rklings: loaded` and return `0` only after both channels exist.

Never pass an error pointer to `rk_channel_destroy`.

### TODO 2 -- `core/main.c:55:2` -- Destroy both channels

1. Log `rklings: unloaded`.
2. Destroy channel B, then clear `chan_b`.
3. Destroy channel A, then clear `chan_a`.

The kernel calls this function only after a successful load.

## Test it

Build the normal variant and enter the VM:

```console
$ ./rklings run L01
```

Test the successful build inside the VM:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# dmesg | tail -1
rklings: loaded
root@victim:~# rmmod rklings
```

This manual test covers the successful path.
The validator builds and tests the failure variants separately.

## Validate

```console
$ ./rklings validate L01
  [+] Normal load and unload
  [+] First allocation rejection leaves no module
  [+] Second allocation rejection releases the first channel
  PASS
```

## Next

Module loading can now fail without leaving created resources behind.
L02 turns the first owned resource into module state that remains available
until unload.
