# 17 - Run code later with a timer

A kernel timer schedules a callback after a deadline measured in `jiffies`.
Drivers use timers for retry deadlines, polling, and protocol timeouts when the callback can finish without sleeping.

## Observe it

The module exposes the number of timer firings through `/proc/rk_beacon`.

```text
load                    after one second          after two seconds
  |                            |                         |
  v                            v                         v
count=0  ---- timer ---->   count=1  ---- re-arm ----> count=2
```

```bash
cat /proc/rk_beacon
sleep 2
cat /proc/rk_beacon
```

## How it works

`timer_setup(&beacon_timer, beacon_fire, 0)` initializes the timer and binds its callback.
`mod_timer(&beacon_timer, jiffies + HZ)` sets the deadline to approximately one second from now.
`jiffies` is the kernel tick counter, while `HZ` is the number of ticks per second.

A timer callback runs in atomic context.
It must not sleep, perform blocking I/O, or allocate memory with `GFP_KERNEL`.
Use a workqueue when the delayed action may sleep.

The callback is the only writer.
It publishes the counter with `WRITE_ONCE()`, while procfs samples it with `READ_ONCE()`.
This prevents compiler-generated torn or repeated accesses; it would not make multiple writers safe.
`timer_shutdown_sync()` prevents re-arming and waits for an active callback before module memory is released.

## Implement it

Complete `beacon_fire()` so it increments `beacon_count` and re-arms `beacon_timer` for `jiffies + HZ`.
Complete `beacon_init()` so it schedules the initialized timer's first firing.

## Verify

```bash
./rootkitlings start L17
$EDITOR module/hello.c
./rootkitlings check L17
```

The check reads the real counter before and after a delay, then verifies synchronous cleanup.
