# Publish and roll back a procfs entry

Module state is private until the module publishes an interface for userspace.
This lesson adds `/proc/rootkitlings` after the state object from the previous
lesson is ready.

The order matters:

```text
init:     rk_state_create()   -> rk_proc_publish()
teardown: rk_proc_unpublish() -> rk_state_destroy()
```

The exit callback is not called when module initialization fails.
If procfs publication fails after state creation, the init callback must destroy
state before it returns the publication error.

## The procfs owner

`interface/proc.c` alone stores the `struct proc_dir_entry *`.
It publishes the entry with `proc_create_single()` and removes the entry with
`proc_remove()`.

The supplied show function returns one fixed line, `active`, so the read-only
entry has real behavior from the moment it appears.
This lesson owns publication and cleanup.
State snapshots are outside this exercise.

## Failure point 3

Failure point 3 makes the publication result `NULL` after state exists.
It then follows the same error path as a real `proc_create_single()` allocation
failure: return `-ENOMEM` and let the init callback roll back state.
The init callback preserves that error and destroys the state it already owns.

## Your task

Starting from your passed state-ownership lesson, this lesson adds
`interface/proc.c`.
Edit these four files:

- Add `interface/proc.o` to `rootkitlings-y` in `Kbuild`.
- Extend `include/rootkit.h` with failure point 3 and the status publication
  functions.
- In `interface/proc.c`, use the supplied show function to create
  `/proc/rootkitlings` with mode `0444`, keep its entry pointer private, return
  `-ENOMEM` on a real creation failure, and clear the pointer after removal.
- In `core/main.c`, publish only after state creation.
  On publication failure, destroy state and return the original error.
  On unload, remove the proc entry before destroying state.

Do not publish before state exists, remove the proc entry after destroying
state, or replace an error returned by a component with a different one.

## Check and observe

Run:

```bash
./rootkitlings check
```

The checker builds failure points 0 through 3 without warnings and rejects an
unknown value.
A host fixture injects independent state and procfs operations to verify
acquisition order, reverse-order cleanup, exact error propagation, and the
publication API contract.
A disposable guest verifies that `/proc/rootkitlings` exists with mode `0444`
and content `active` only while the module is loaded.
It also checks the exact errors for all three rejected loads.

For failure point 3, kernel allocation trace events must show that a module
allocation is passed to `kfree()` during rollback; object-file checks enforce
the component ownership boundary.
Module logs are not accepted as cleanup evidence.

## Official references

- [Procfs API declarations][ref-1]
- [The seq_file interface](https://docs.kernel.org/filesystems/seq_file.html)
- [Driver basics: module initialization and exit][ref-2]
- [Centralized function exits][ref-3]

[ref-1]:
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/linux/proc_fs.h

[ref-2]:
  https://docs.kernel.org/driver-api/basics.html#c.module_init

[ref-3]:
  https://docs.kernel.org/process/coding-style.html#centralized-exiting-of-functions
