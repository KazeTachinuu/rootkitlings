# 23 - Wake a thread on demand

A wait queue parks a thread until a condition becomes true.
Use it for event-driven workers that should consume no CPU while idle and react without polling delays.

## Observe it

Writing `wake` to `/proc/rk_gate` publishes an event and wakes the worker.

```bash
cat /proc/rk_gate
printf wake > /proc/rk_gate
cat /proc/rk_gate
```

```text
proc writer             shared condition              worker thread
    |                           |                            |
    +-- pending = 1 ----------->|                            |
    +-- wake_up_interruptible() +--------------------------->|
                                |                 condition true
                                |<---------- pending = 0 ----+
```

## How it works

`wait_event_interruptible(queue, condition)` sleeps until the condition is true or a signal interrupts the wait.
The condition is checked again after every wakeup because a wakeup does not itself prove that work exists.
`wake_up_interruptible()` makes eligible waiters runnable.

The pending flag is atomic because the proc writer and kernel thread access it concurrently.
The worker consumes it with `atomic_xchg(&heartbeat_pending, 0)`, which tests and clears the event as one operation.
This one-bit flag coalesces writes that arrive before the worker consumes the event.
A counted queue is required when every event must be preserved.

Teardown sets the condition and wakes the queue before calling `kthread_stop()`.
That ordering prevents unload from waiting forever on a sleeping thread.

## Implement it

After the supplied parser accepts the exact command `wake`, call `atomic_set(&heartbeat_pending, 1)` and then `wake_up_interruptible(&heartbeat_wq)`.

Unknown, empty, and oversized commands are rejected without publishing an event.

`heartbeat_pending` is a condition flag, not a queue depth.
Several writes before the worker runs intentionally coalesce into one wake cycle.

## Verify

```bash
./rootkitlings start L23
$EDITOR module/hello.c
./rootkitlings check L23
```

The check sends two real events and verifies that the worker consumes both.
