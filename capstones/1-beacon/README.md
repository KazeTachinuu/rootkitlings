# Capstone 1: Beacon

Build a module that runs a background heartbeat. Every C2 implant has
a beacon loop: wake up, check in, go back to sleep. This is yours.

```
  module load
       |
       v
  kthread_run("rk_beacon")
       |
       v
  +---------------------------+
  |  beacon loop              |      /proc/rk_beacon
  |                           |      +------------------+
  |  while (!should_stop) {   |      | uptime: 45s      |
  |    mutex_lock(&state)     |----->| ticks: 9         |
  |    ticks++                |      | interval: 5      |
  |    mutex_unlock(&state)   |      | status: running  |
  |    ssleep(interval)       |      +------------------+
  |  }                        |
  +---------------------------+      /sys/.../interval
                                     echo 10 > interval
                                     (callback updates interval)
```

## Prerequisites

Lings 00 through 23 (foundation + interfaces + concurrency).

## What you are building

A kernel thread that wakes up every N seconds, increments a counter,
and reports its state through `/proc/rk_beacon`.

- A kthread runs the beacon loop.
- A sysfs parameter `interval` controls the sleep duration (default 5s).
- A parameter callback validates the new interval (must be 1-300).
- A mutex protects the shared state (tick count, interval, start time).
- `/proc/rk_beacon` reports: uptime, tick count, interval, status.
- On unload, the thread stops cleanly via `kthread_stop`.

## What "done" looks like

```sh
insmod hello.ko
sleep 12
cat /proc/rk_beacon
# uptime: 12s
# ticks: 2
# interval: 5
# status: running
echo 2 > /sys/module/hello/parameters/interval
sleep 5
cat /proc/rk_beacon
# uptime: 17s
# ticks: 4
# interval: 2
# status: running
rmmod hello
```

## What a defender sees

The kthread shows up in `ps`:

```
ps aux | grep rk_beacon
root     [rk_beacon]
```

The thread name is visible in the process list. Renaming it changes only the
label; its task, wakeups, module code, and behavior remain observable.

## Contract

The check script verifies:

- `/proc/rk_beacon` exists after loading.
- `ticks` increments over time (at least 2 ticks in 12 seconds at default interval).
- `interval` defaults to 5.
- Changing `interval` via sysfs changes the tick rate.
- `status` is `running` while loaded.
- `/proc/rk_beacon` disappears after unload.
- No kernel warnings or hung thread on unload.

## Verify

```bash
./rootkitlings check capstone-1
```

## Hints

Only if stuck. Try without them first.
