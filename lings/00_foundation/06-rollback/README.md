# 06 - Failed init rolls ownership back

## Why this step

Ling 05 handled complete success: init allocated one object and exit freed it.
But initialization usually has several steps. If step one succeeds and step two
fails, exit will not run because the module never finished loading.

Init must clean up its own partial work before returning an error.

## The rollback pattern

The kernel convention for multi-step init uses `goto` labels to unwind
in reverse order:

```
                  init
                      |
              kmalloc(state)
                      |
                  +---+---+
                  |       |
               fail?    ok
                  |       |
            return      fail_second?
           -ENOMEM        |
                      +---+---+
                      |       |
                    yes       no
                      |       |
                      |   kmalloc(second)
                      |       |
                      |   +---+---+
                      |   |       |
                      |  fail?    ok
                      |   |       |
                      v   v       v
                 free_state:   return 0
                  kfree(state)    |
                  return error    v
                      |       exit:
                      v        kfree(second)
                  not loaded    kfree(state)
                  exit never
                    runs
```

Two different code paths free `state`: the `goto` label in init (on
failure), and exit (on success). Only one of them runs for a given load.

## Build it

Keep `state` from ling 05. Add:

- A second `struct rk_state *second`.
- A boolean module parameter `fail_second` (default false). Use
  `module_param(fail_second, bool, 0444)` from `linux/moduleparam.h`.

In your init function:

1. Allocate `state` (ling 05 pattern).
2. If `fail_second` is true, set an error and `goto` the cleanup label.
3. Otherwise, allocate `second`.
4. If `second` allocation fails, also `goto` the cleanup label.
5. If both succeed, log success and return 0.
6. The cleanup label frees `state` and returns the error.

In your exit function, free both in reverse order (second, then state).

## Predict

- Does exit run when `fail_second=1`?
- Which path frees `state` after that failure?
- Which path owns both objects after init returns `0`?

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko fail_second=1
echo "status=$?"
lsmod
dmesg | tail
insmod hello.ko
rmmod hello
dmesg | tail
```

## Contract

The check script verifies:

- With `fail_second=1`: `insmod` fails (module is rejected).
- Without args: `insmod` succeeds and the module loads.
- After `rmmod`: the module unloads cleanly.

## Verify

```bash
./rootkitlings check 6
```

<details>
<summary>Explain the result</summary>

Before the first allocation, init owns nothing.
After it succeeds, every later error path owns and must free `state`.
After both allocations succeed and init returns zero, the loaded module owns
both and exit inherits both cleanup obligations.

Cleanup normally runs in reverse acquisition order because later resources may
depend on earlier ones. One label also keeps all paths consistent as the
function grows.

A direct `return error` after acquiring `state` would leak it because failed
init has no later exit callback.

</details>

> **What a defender sees**: the module owns its memory safely, but userspace has no way
> to interact with it beyond sysfs parameters. It needs a real interface.

See also: [07 - Procfs entry](../07-proc-entry/README.md).
