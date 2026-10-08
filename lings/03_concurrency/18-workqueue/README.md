# 18 - Defer work to process context

A workqueue runs a callback in a kernel worker thread.
Use it when an interrupt, timer, or probe detects an event but the resulting work may sleep.

## Observe it

The timer only queues work.
The worker increments the counter exposed at `/proc/rk_worker`.

```text
timer callback                 kernel worker
      |                              |
      +-- schedule_work() ---------->+-- beacon_run()
      |                              |      count++
      +-- re-arm timer               +-- may sleep
```

```bash
cat /proc/rk_worker
sleep 2
cat /proc/rk_worker
```

## How it works

`INIT_WORK(&beacon_work, beacon_run)` binds a `work_struct` to its worker function.
`schedule_work(&beacon_work)` queues it on the system workqueue.
Repeated scheduling while the same work item is pending is coalesced rather than creating parallel copies.

Teardown stops the timer first so it cannot queue new work.
`cancel_work_sync()` then cancels pending work or waits for a running worker.

## Implement it

In `beacon_fire()`, queue `beacon_work` with `schedule_work()`.
Keep the existing timer re-arm after it so the event remains periodic.

## Verify

```bash
./rootkitlings start L18
$EDITOR .rootkitlings/work/workqueue/hello.c
./rootkitlings check L18
```

The check observes the worker-owned counter and verifies cleanup.
