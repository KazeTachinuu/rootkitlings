# Dead-man watchdog

A controller may stop responding without closing its connection cleanly.

A dead-man watchdog records periodic heartbeats and marks the peer stale when no heartbeat arrives before a deadline.

This module accepts `ping` through `/proc/rk_watchdog` and reports the live state through the same entry.

## Operate it

Load the module and inspect its initial state:

```sh
insmod hello.ko
cat /proc/rk_watchdog
```

```text
state=healthy
heartbeats=0
timeouts=0
age_ms=12
timeout_ms=2000
```

The exact age depends on when the file is read.

Send a heartbeat once per second:

```sh
while sleep 1; do echo ping > /proc/rk_watchdog; done
```

While heartbeats continue, `state` remains `healthy` and `heartbeats` increases.

Stop that loop and wait for the two-second deadline:

```text
state=stale
heartbeats=8
timeouts=1
age_ms=2041
timeout_ms=2000
```

Another `ping` immediately restores `healthy` state.

## Heartbeat lifecycle

```text
write "ping"                     deadline expires
     |                                  |
     v                                  v
record time                       healthy -> stale
heartbeats++                      timeouts++ once
mark healthy
     |
     v
wake worker and restart deadline
```

The proc callback and worker protect the timestamp, counters, and stale flag with one mutex.

An atomic generation number lets the wait condition notice a heartbeat without taking that mutex.

Each heartbeat starts a fresh deadline.

The worker counts only the transition from healthy to stale, so one uninterrupted outage contributes one timeout.

## Files

```text
main.c       module lifecycle
watchdog.c   proc interface, state, wait queue and worker
watchdog.h   component lifecycle contract
```

`watchdog_start()` creates the proc entry before the thread and removes the entry if thread creation fails.

`watchdog_stop()` joins the worker before removing the proc entry.

## Your work

Complete the successful `ping` path in `watchdog_write()`.

Under `state_lock`, record `jiffies` in `last_heartbeat`, increment `heartbeats`, and clear `stale`.

Then increment `heartbeat_generation`, wake `heartbeat_wait`, and return the original write length.

Complete `watchdog_loop()`.

Remember the generation observed before waiting.

After a heartbeat wake, remember the new generation and begin a fresh timeout.

After an expired wait, lock the state and mark it stale only when the deadline has passed and it was previously healthy.

Increment `timeouts` on that transition and stop promptly when `kthread_should_stop()` becomes true.

## Verify

```sh
./rootkitlings check C02
```

The verification sends real heartbeats, waits for an actual timeout, proves one outage is counted once, restores healthy state, and unloads the worker cleanly.

The proc entry and `rk_watchdog` thread expose the mechanism to ordinary inspection.
