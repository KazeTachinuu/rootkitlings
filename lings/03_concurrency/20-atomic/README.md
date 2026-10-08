# 20 - Update a simple counter atomically

An atomic variable performs a small state transition as one indivisible operation.
Use it for independent counters and flags that do not require a larger multi-field invariant.

## Observe it

Two workers each perform 10,000 increments.
`/proc/rk_atomic` reports their state.

```text
thread A -- 10,000 atomic_inc() --+
                                      +--> count=20000
thread B -- 10,000 atomic_inc() --+
```

```bash
cat /proc/rk_atomic
# count=20000 running=0 finished=2
```

## How it works

`ATOMIC_INIT(0)` initializes an `atomic_t`.
`atomic_inc()` updates it without a mutex or a lost-update race.
`atomic_read()` retrieves the current value.

Atomic operations do not turn a multi-step algorithm into a transaction.
Use a lock when several fields must change together, when check-then-act must be indivisible, or when protected code may sleep.

The separate `finished` counter distinguishes “no worker has started” from “both workers have completed.”
The supplied lifecycle stops and joins both workers with `kthread_stop()` before unloading; the next exercise introduces completions for signaling a particular event.

## Implement it

Increment `shared_counter` with `atomic_inc()` inside the worker loop.

## Verify

```bash
./rootkitlings start L20
$EDITOR module/hello.c
./rootkitlings check L20
```

The check waits for `finished=2` and then requires the exact count and zero running workers.
