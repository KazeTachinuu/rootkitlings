# 22 - Mutex

## Why this step

Two workers now update one counter. A mutex serializes the read-modify-write
operation so no update can be lost.
Only one thread holds the lock at a time. The loser sleeps until the
holder releases it.

## What you need

You already know `kthread_run` and the shared counter pattern from
ling 21. This ling adds shared state and the lock that owns its invariant.

New includes:

```c
#include <linux/mutex.h>   // DEFINE_MUTEX, mutex_lock, mutex_unlock
```

## Build it

Create a two-thread counter and protect each increment with a mutex:

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

Wrap the shared read-modify-write operation in
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

## Contract

The check script verifies:

- `/proc/rk_mutex` exists after loading.
- After threads complete, `/proc/rk_mutex` returns exactly `20000` (mutex eliminates the race).
- `/proc/rk_mutex` disappears after unload.

## Verify

```bash
./rootkitlings check 22
```

> **What a defender sees**: a mutex held for too long blocks the thread.
> If a rootkit's mutex deadlocks, the module becomes unloadable, which
> is visible as a stuck `rmmod` process.

See also: [23 - Atomic](../23-atomic/README.md).
