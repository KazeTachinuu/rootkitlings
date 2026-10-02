# 16 - Kernel thread

## Why this step

A workqueue borrows a shared thread pool. A kernel thread gives you
a dedicated loop that runs until told to stop. This is what drives
a C2 beacon: a persistent loop, sleeping between check-ins, invisible
in the process list as a bracketed kernel thread.

```
  insmod                                    rmmod
    |                                         |
    v                                         v
  kthread_create ----+               kthread_stop (blocks)
  wake_up_process    |                        |
                     v                        v
              +-------------+          should_stop = true
              | thread loop |          thread sees it, exits
              |  count++    |          kthread_stop returns
              |  ssleep(1)  |
              |  check stop |
              +-------------+
```

## What you already know

From ling 08: proc entries. From ling 15: deferred kernel work.

## New API

```c
#include <linux/kthread.h>   // kthread_create, wake_up_process, kthread_stop
#include <linux/delay.h>     // ssleep
```

| API | Purpose |
|-----|---------|
| `kthread_create(fn, data, name)` | Create a kernel thread (does NOT start it) |
| `wake_up_process(task)` | Start the thread |
| `kthread_should_stop()` | Check if someone called `kthread_stop` |
| `kthread_stop(task)` | Ask thread to stop, block until it exits |
| `ssleep(seconds)` | Sleep for N seconds |

The thread function signature:

```c
static int my_thread_fn(void *data);
```

It returns 0 when done. The `data` pointer is whatever you passed
as the second argument to `kthread_create`.

## Build it

Create a module that:

1. Spawns a kthread with `kthread_create`, then starts it with `wake_up_process`.
2. The thread runs a loop: increment a counter, `ssleep(1)`, check `kthread_should_stop`.
3. `/proc/rk_thread` returns the counter.
4. Exit calls `kthread_stop` (blocks until the thread finishes).

`kthread_create` returns a `struct task_struct *` (or an `ERR_PTR` on
failure, handle it the way you did in ling 09).

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
sleep 3
cat /proc/rk_thread       # should be 2 or 3
ps | grep rk       # shows [rk_thread] kernel thread
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_thread` exists after loading.
- After 3 seconds, counter is greater than 1.
- `/proc/rk_thread` disappears after unload.

## Verify

```bash
./rootkitlings check 16
```

> **What a defender sees**: `ps aux` shows a kernel thread with your
> module's name. Kernel threads show as `[bracketed]` names. A thread
> named `[rk_thread]` is suspicious.

See also: [17 - Waiting](../17-waiting/README.md).
