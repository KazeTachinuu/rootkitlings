# 20 - Atomic

## Why this step

A mutex protects multi-step critical sections. For a single variable
(a flag, a counter), `atomic_t` provides lock-free operations. Faster,
simpler, but limited to one variable at a time.

## The concept

```
  Without atomic (the race from ling 18):

  Thread A                Thread B
  read counter (500)      read counter (500)
  add 1 (501)             add 1 (501)
  write counter (501)     write counter (501)
                          ^-- lost update!

  With atomic_inc:

  Thread A                Thread B
  atomic_inc (500->501)        |  (hardware locked)
       |                  atomic_inc (501->502)
       v                       v
  both increments preserved
```

`atomic_t` uses hardware-level locking (locked instructions on x86)
so the read-modify-write happens as one indivisible step.

## What you need

You already know the two-thread counter pattern from lings 18 and 19.
This ling replaces the mutex with `atomic_t`.

New includes:

```c
#include <linux/atomic.h>   // atomic_t, ATOMIC_INIT, atomic_inc, atomic_read
```

## Build it

Create a module that:

1. Uses `atomic_t` for a counter initialized with `ATOMIC_INIT(0)`.
2. Two kthreads increment it 10000 times each with `atomic_inc`.
3. The final count is exactly 20000 (no race).
4. Uses a second `atomic_t` for a `running` flag: set to 1 on load, 0 on exit.
5. `/proc/rk_atomic` returns `count=<N> running=<0|1>`.

The atomic API:

```c
atomic_t var = ATOMIC_INIT(0);    // declare + initialize
atomic_inc(&var);                 // increment (lock-free)
atomic_read(&var);                // read current value
atomic_set(&var, value);          // set to value
```

Take ling 18's `increment_fn` and replace `counter++` with
`atomic_inc`. No mutex needed, no lock/unlock wrapper. Read
the values in your proc callback with `atomic_read`.

## Predict

- Can `atomic_inc` be called from interrupt context?
- If you need to update two atomics together, is that safe?
- When would you use `atomic_t` vs a mutex?

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
sleep 2
cat /proc/rk_atomic       # count=20000 running=1
rmmod hello
```


## Contract

The check script verifies:

- `/proc/rk_atomic` exists after loading.
- After threads complete, `count=20000` (atomic operations eliminate the race).
- `running=0` (both threads finished).
- `/proc/rk_atomic` disappears after unload.

## Verify

```bash
./rootkitlings check 20
```

> **What a defender sees**: atomic operations leave no lock contention
> traces. No mutex means no deadlock risk, but also no multi-field
> protection.

See also: [Introspection](../../03_introspection/README.md).
