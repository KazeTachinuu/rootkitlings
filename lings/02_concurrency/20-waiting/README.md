# 20 - Waiting

## Why this step

In ling 19 the thread busy-polls with `ssleep`. That wastes CPU when
there is nothing to do. A wait queue lets the thread sleep until an
external event wakes it, using zero CPU in between.

```
                   proc write
                   "wake"
                      |
                      v
  kthread          wake_up_interruptible
  +--------+              |
  | SLEEP  |<-- flag=0 ---+
  |        |              |
  | wake?  |--- flag=1 -->|
  |   yes  |              v
  |        |         count++
  | reset  |         flag = 0
  | flag   |         go back to SLEEP
  +--------+
```

The thread does nothing until woken. A beacon that sleeps until
commanded is harder to detect than one that loops.

## What you already know

From ling 19: kthreads, `kthread_should_stop`. From ling 12: proc
write handlers.

## New API

```c
#include <linux/wait.h>
```

| API | Purpose |
|-----|---------|
| `DECLARE_WAIT_QUEUE_HEAD(name)` | Declare a wait queue (global) |
| `wait_event_interruptible(wq, condition)` | Sleep until condition is true |
| `wake_up_interruptible(&wq)` | Wake all threads sleeping on this queue |

The condition in `wait_event_interruptible` is re-evaluated each time
the thread is woken. If it is still false, the thread goes back to sleep.

## Build it

Create a module that:

1. A kthread (from ling 19) waits on a wait queue for a flag to be set.
2. Writing `"wake"` to `/proc/rk_gate` sets the flag and calls `wake_up_interruptible`.
3. The thread logs each wake event, resets the flag, and increments a counter.
4. Reading `/proc/rk_gate` returns the wake count.

Two things to get right:

- The `wait_event_interruptible` condition must also check `kthread_should_stop()`,
  otherwise the thread cannot be woken for cleanup.
- In your exit function, set the flag and wake the thread before calling
  `kthread_stop`, or stop will deadlock on a sleeping thread.

Create `/proc/rk_gate` with mode `0666` (readable and writable).

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_gate                  # 0
printf 'wake' > /proc/rk_gate
cat /proc/rk_gate                  # 1
printf 'wake' > /proc/rk_gate
cat /proc/rk_gate                  # 2
rmmod hello
```

## Hints

## Contract

The check script verifies:

- `/proc/rk_gate` must output lines in this format:
  ```
  N
  ```
- `/proc/rk_gate` returns the wake count as a plain number (starts at 0).
- Writing `wake` to `/proc/rk_gate` wakes the thread, which increments the counter.

## Verify

```bash
./rootkitlings check 20
```

> **What a defender sees**: the thread sits idle in `wait_event`, consuming
> zero CPU. It looks like any sleeping kthread, harder to spot than a
> busy-polling loop.

See also: [21 - Shared state](../21-shared-state/README.md).
