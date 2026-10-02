# 15 - Workqueue

## Why this step

Your ling 14 timer fires in softirq context: no sleeping, no blocking
allocations, no I/O. Real rootkit tasks (exfiltration, file writes,
network calls) need process context. A workqueue bridges the gap.

```
  softirq context                   process context
  (no sleeping)                     (sleeping OK)
  +------------+                    +------------------+
  | timer fires| -- schedule_work ->| work function    |
  |            |                    | can kmalloc, I/O |
  +------------+                    +------------------+
```

The timer decides WHEN. The workqueue decides WHERE (in which context).

## What you already know

From ling 14: `timer_setup`, `mod_timer`, timer callbacks. From ling 08:
proc entries and `simple_read_from_buffer`.

## New API

One new header on top of ling 14:

```c
#include <linux/workqueue.h>
```

The key types and functions:

| API | Purpose |
|-----|---------|
| `struct work_struct` | Represents a deferred work item |
| `INIT_WORK(&work, callback)` | Bind a callback to a work item |
| `schedule_work(&work)` | Queue the work for execution |
| `cancel_work_sync(&work)` | Wait for pending work to finish, then cancel |

The work callback signature:

```c
static void my_work_fn(struct work_struct *work);
```

## Build it

Create a module that:

1. Declares a `work_struct` and a `timer_list` (you did timers in ling 14).
2. The timer fires every second and calls `schedule_work` to queue the work.
3. The work function increments a counter and logs it.
4. `/proc/rk_worker` returns the counter (same pattern as ling 08).
5. Exit cleans up in the right order.

Think about cleanup order: if you cancel the work first, the timer could
schedule new work between your cancel and timer stop. Stop the source
(timer) before draining the sink (work).

## Predict

- What is the difference between `schedule_work` and `queue_work`?
- Why cancel the timer before the work, not the other way around?
- Can two instances of the same work item run concurrently?

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
sleep 3
cat /proc/rk_worker       # should be 2 or 3
dmesg | tail
rmmod hello
```


## Hints

<details>
<summary>Hint 1: the work callback signature</summary>

The work function receives a pointer to the `work_struct` itself, not
custom data. Declare the counter as a module-level global, not as
something passed to the work function.

```c
static void my_work_fn(struct work_struct *work)
{
    // access global counter directly
}
```

</details>

<details>
<summary>Hint 2: INIT_WORK goes in your init function</summary>

`INIT_WORK(&work, my_work_fn)` binds the callback to the work item.
Call it once in init, before arming the timer. The timer callback then
calls `schedule_work(&work)` each time it fires.

</details>

<details>
<summary>Hint 3: cleanup order matters</summary>

Stop the timer first, then cancel the work:

```c
timer_shutdown_sync(&beacon_timer);   // stop the source
cancel_work_sync(&work);             // drain the sink
```

If you cancel work first, the timer could schedule new work between
your cancel and your timer stop.

</details>

## Contract

The check script verifies:

- `/proc/rk_worker` exists after loading.
- After 3 seconds, counter is greater than 1.
- `/proc/rk_worker` disappears after unload.

## Verify

```bash
./rootkitlings check 15
```

> **What a defender sees**: workqueue threads appear in `ps` as
> `kworker/` threads. Legitimate kernel subsystems use them too, so they
> blend in better than a dedicated kthread.

See also: [16 - Kernel thread](../16-kthread/README.md).
