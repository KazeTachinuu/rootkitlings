# 17 - Waiting

## Why this step

In ling 16 the thread busy-polls with `ssleep`. That wastes CPU when
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

From ling 16: kthreads, `kthread_should_stop`. From ling 10: proc
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

1. A kthread (from ling 16) waits on a wait queue for a flag to be set.
2. Writing `"wake"` to `/proc/rk_gate` sets the flag and calls `wake_up_interruptible`.
3. The thread logs each wake event, resets the flag, and increments a counter.
4. Reading `/proc/rk_gate` returns the wake count.

Two things to get right:

- The `wait_event_interruptible` condition must also check `kthread_should_stop()`,
  otherwise the thread cannot be woken for cleanup.
- In your exit function, set the flag and wake the thread before calling
  `kthread_stop`, or stop will deadlock on a sleeping thread.

Create `/proc/rk_gate` with mode `0666` (readable and writable).

## Predict

- What happens if `wake_up` is called before `wait_event`?
- Can `wait_event_interruptible` return early?
- What is the "thundering herd" problem?

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

<details>
<summary>Hint 1: the condition must include kthread_should_stop</summary>

`wait_event_interruptible` blocks until its condition is true. If your
condition is just `flag`, the thread cannot be woken for cleanup.
Combine them:

```c
wait_event_interruptible(wq, flag || kthread_should_stop());
```

After waking, check `kthread_should_stop()` first. If true, break out
of the loop.

</details>

<details>
<summary>Hint 2: wake the thread before stopping it</summary>

`kthread_stop` waits for the thread to exit. If the thread is sleeping
in `wait_event_interruptible`, it will never wake up and `kthread_stop`
deadlocks. In your exit function:

1. Set the flag to true.
2. Call `wake_up_interruptible(&wq)`.
3. Then call `kthread_stop(task)`.

</details>

<details>
<summary>Hint 3: reset the flag after processing</summary>

The thread loop should: wake up, check `kthread_should_stop`, increment
the counter, reset the flag to false, then loop back to
`wait_event_interruptible`. If you forget to reset the flag, the thread
will never sleep again (the condition stays true).

</details>

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
./rootkitlings check 17
```

> **What a defender sees**: the thread sits idle in `wait_event`, consuming
> zero CPU. It looks like any sleeping kthread, harder to spot than a
> busy-polling loop.

See also: [18 - Shared state](../18-shared-state/README.md).
