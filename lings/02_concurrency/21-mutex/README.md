# 21 - Mutex

## Why this step

Ling 18 showed lost updates. A mutex serializes access to shared state.
Only one thread holds the lock at a time. The loser sleeps until the
holder releases it.

## What you need

You already know `kthread_run` and the shared counter pattern from
ling 20. This ling adds a mutex to eliminate the race.

New includes:

```c
#include <linux/mutex.h>   // DEFINE_MUTEX, mutex_lock, mutex_unlock
```

## Build it

Take ling 20's two-thread counter and add a mutex:

1. Define `DEFINE_MUTEX(rk_lock)`.
2. Each thread locks before incrementing and unlocks after.
3. The final count should be exactly 20000.
4. `/proc/rk_mutex` returns the count.

```
  thread A              thread B
  mutex_lock            mutex_lock (sleeps)
  counter++ (500->501)       |
  mutex_unlock               |
       |                mutex_lock (wakes)
       |                counter++ (501->502)
       |                mutex_unlock
  no lost update
```

The mutex API:

```c
DEFINE_MUTEX(name);         // declare + initialize at file scope
mutex_lock(&name);          // acquire (sleeps if held)
mutex_unlock(&name);        // release
```

Take ling 20's `increment_fn` and wrap the shared access in
`mutex_lock`/`mutex_unlock`. Lock per iteration, not around the
entire loop.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
sleep 2
cat /proc/rk_mutex       # exactly 20000
dmesg | tail
rmmod hello
```

## Hints

## Contract

The check script verifies:

- `/proc/rk_mutex` exists after loading.
- After threads complete, `/proc/rk_mutex` returns exactly `20000` (mutex eliminates the race).
- `/proc/rk_mutex` disappears after unload.

## Verify

```bash
./rootkitlings check 19
```

> **What a defender sees**: a mutex held for too long blocks the thread.
> If a rootkit's mutex deadlocks, the module becomes unloadable, which
> is visible as a stuck `rmmod` process.

See also: [20 - Atomic](../20-atomic/README.md).
