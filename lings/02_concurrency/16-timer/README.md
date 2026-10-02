# 16 - Timer

## Why this step

A rootkit beacon needs periodic callbacks. The kernel's timer API
fires a function after a delay. The callback runs in softirq context
(not process context), so it cannot sleep.

## What you need

You already know how to create `/proc/rk_beacon` with `proc_create_single`
and `seq_file` from ling 11. This ling adds a repeating timer that
increments a counter readable from procfs.

New includes (on top of ling 10):

```c
#include <linux/jiffies.h>   // jiffies, HZ
#include <linux/timer.h>     // timer_list, timer_setup, mod_timer
```

## Build it

Create a module that:

1. Sets up a timer with `timer_setup` that fires 1 second after load.
2. The callback increments a counter and re-arms the timer with `mod_timer`.
3. `/proc/rk_beacon` returns the current counter value.
4. Exit cancels the timer with `timer_shutdown_sync` (modern API).

```
  insmod                              time
  --+-----------+-----------+-----------+----> rmmod
    |           |           |           |
    v           v           v           v
  timer_setup  callback    callback    timer_shutdown_sync
  mod_timer    count=1     count=2     (permanent cancel)
               mod_timer   mod_timer
```

The timer API:

```c
timer_setup(&timer, callback, 0);          // initialize
mod_timer(&timer, jiffies + HZ);           // arm: fires in 1 second
timer_shutdown_sync(&timer);               // permanent cancel (for exit)
```

The callback re-arms the timer to create a repeating beacon:

```c
static void beacon_callback(struct timer_list *t)
{
    counter++;
    mod_timer(t, jiffies + HZ);   // re-arm: fire again in 1 second
}
```

`timer_shutdown_sync` waits for any running callback to finish, then
permanently disables the timer. Call it in your exit function before
`proc_remove`. The older `del_timer_sync` was removed in kernel 6.15.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_beacon       # should print 0
sleep 3
cat /proc/rk_beacon       # should print 2 or 3
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_beacon` exists after loading.
- Initial counter value is `0`.
- After 3 seconds, counter is greater than 1.
- `/proc/rk_beacon` disappears after unload.

## Verify

```bash
./rootkitlings check 14
```

> **What a defender sees**: the timer itself is invisible, but its effects
> (procfs updates, dmesg entries) run on a regular schedule. Predictable
> timing is a detection signal.

See also: [15 - Workqueue](../15-workqueue/README.md).
