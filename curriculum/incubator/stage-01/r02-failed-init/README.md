# Reject module initialization precisely

A module becomes resident only if its load callback returns `0`.
If initialization returns a negative Linux error number, the loader reports that error and the module never becomes loaded.

A deterministic rejected path lets you verify the kernel's failed-initialization contract instead of waiting for an accidental failure.

## One build, two deterministic paths

This exercise selects the path at build time:

```text
ROOTKITLINGS_FAIL_POINT=0        ROOTKITLINGS_FAIL_POINT=1
            |                                |
            v                                v
      init returns 0                 init returns -ECANCELED
            |                                |
            v                                v
 module appears in inventory       module remains absent
            |
            v
   rmmod calls module exit
```

Kbuild's `?=` operator supplies a default without overriding a value selected by the checker.
The compiler's `-D` option makes that integer available to C as a preprocessor macro:

```make
ROOTKITLINGS_FAIL_POINT ?= 0
ccflags-y += -DROOTKITLINGS_FAIL_POINT=$(ROOTKITLINGS_FAIL_POINT)
```

Only values `0` and `1` are valid here.
An unknown value must stop compilation rather than silently exercise the normal path.

`-ECANCELED` is the negative Linux error for a canceled operation.
Returning it preserves a precise reason for rejection.
Because failed initialization never installs the module, the kernel does not call its registered exit callback.
That is part of the kernel lifecycle contract; module inventory proves that the module remained absent, not whether a callback ran.
This module acquires no resource, so the rejected path has nothing to release.

## Your task

Start this lesson to copy your passed module forward, then edit its two existing files.
Do not add files.

- In `Kbuild`, define `ROOTKITLINGS_FAIL_POINT` with default `0` and pass it
  to C.
- In `core/main.c`, define `RK_FAIL_NONE` as `0` and `RK_FAIL_PREFLIGHT` as
  `1`.
- Reject unknown build-time values with a compile-time error.
- Return `-ECANCELED` from init when the selected value is
  `RK_FAIL_PREFLIGHT`.
- Preserve the existing load and unload behavior for value `0`.
- Preserve the existing license, description, artifact name, and module name.

The module must build without warnings.
Do not call the exit callback from the rejected init path; failed init is not unload.

## Check and observe

Run:

```bash
./rootkitlings check
```

The checker builds value `1`, boots a disposable guest, and calls `finit_module()` through a small verifier.
It requires the numeric error to be `ECANCELED`, then repeats the rejected load.
Before and after both attempts, it verifies that `rootkitlings` is absent from both `/proc/modules` and `/sys/module/rootkitlings`.

The checker then rebuilds value `0` in a clean directory and uses a fresh guest to load, unload, and immediately reload the module.
It also proves that an unknown failure-point value cannot compile.

The module is never inserted into the host kernel.
Each guest is diskless and discarded after the check.

A rejected load leaves no entry in `/proc/modules` or `/sys/module`; kernel messages alone do not prove that absence.

## Official references

- [Driver basics: module initialization and exit](https://docs.kernel.org/driver-api/basics.html#c.module_init)
- [Linux kernel error conventions](https://docs.kernel.org/kernel-hacking/hacking.html#return-conventions)
