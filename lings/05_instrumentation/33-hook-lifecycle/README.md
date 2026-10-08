# 33 - Roll back a partially installed hook set

A module that owns several hooks must either install all of them or remove every hook installed before a failure.
Leaving one callback registered after initialization fails points the kernel at module code that will be freed.

## Observe the transaction

The module registers probes for `vfs_read` and `vfs_write`, then creates `/proc/rk_lifecycle`.

```text
register read ---> register write ---> create proc ---> active
      |                  | failure
      |                  v
      +<--------- unregister read
```

Loading with `fail_second=1` deliberately fails after the first registration.
A normal load immediately afterward succeeds only if that first probe was actually removed.

## How it works

Initialization acquires resources in dependency order.
Each failure path releases only the resources already acquired, in reverse order.
Normal exit also removes the proc entry and hooks in reverse order.

`unregister_kprobe()` waits until the handler can no longer execute before returning.
That synchronization must happen before module text and handler-owned state are released.
The counters are atomic because probe callbacks may run simultaneously on multiple CPUs.

## Implement it

When registration of `kp_write` fails, unregister `kp_read` before returning the error.

## Verify

```bash
./rootkitlings start L33
$EDITOR module/hello.c
./rootkitlings check L33
```

The check forces partial initialization, loads normally afterward, exercises both probes, and verifies cleanup.
