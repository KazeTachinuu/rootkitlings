# Beacon scheduler

A beacon wakes periodically to perform a short action, then waits again.

This module implements the timing and control loop without sending network traffic.

It counts completed intervals and exposes a live snapshot through `/proc/rk_beacon`.

## Operate it

Load the module with its five-second default interval:

```sh
insmod hello.ko
cat /proc/rk_beacon
```

```text
uptime: 0s
ticks: 0
interval: 5
status: running
```

After a complete interval, the tick count advances:

```sh
sleep 6
cat /proc/rk_beacon
```

```text
uptime: 6s
ticks: 1
interval: 5
status: running
```

Change the live interval through sysfs:

```sh
echo 1 > /sys/module/hello/parameters/interval
sleep 3
cat /proc/rk_beacon
```

The exact uptime and tick count depend on command timing, but the interval is now one second and ticks advance at the new rate.

## Timer and work

```text
timer expires
     |
     +---- queue work ----> ticks++
     |
     +---- rearm timer
              ^
              |
interval write+---- move next expiry to now + interval
```

The timer callback runs in atomic context and performs no sleeping work.

It queues `beacon_work` on the system workqueue, then rearms itself from the current interval.

The work callback runs in process context and increments the completed tick count.

An interval update uses `mod_timer()` so a change from 300 seconds to one second affects the pending expiry immediately.

`READ_ONCE()` and `WRITE_ONCE()` publish the integer interval, while an atomic counter publishes completed work.

## Files

```text
main.c      start components and unwind failed initialization
beacon.c    parameter, timer, queued work and atomic state
status.c    read-only proc snapshot
beacon.h    worker lifecycle and snapshot contract
status.h    proc-entry lifecycle contract
```

`beacon.c` owns all mutable state.

`status.c` formats a copied snapshot instead of reaching into that state directly.

## Your work

Complete `beacon_timer_expired()` in `beacon.c`.

Return immediately when `running` is false.

Otherwise queue `beacon_work`, then rearm `beacon_timer` for `jiffies + interval * HZ` using the current interval.

Complete the successful update in `interval_set()`.

When the beacon is running, use `mod_timer()` to move the pending expiry to `jiffies + new_interval * HZ`.

The parsing, range validation, atomic tick update, startup, and synchronous timer/work cleanup are provided.

## Verify

```sh
./rootkitlings check C01
```

The verification observes the initial state, rejects an invalid interval without changing it, changes the live interval, measures advancing ticks, and unloads the worker cleanly.

The proc entry, parameter, timer cadence, queued work, and loaded module are all observable.
