# Run a kernel thread

A rootkit agent needs a persistent loop: connect, do work, sleep, repeat.
The kernel provides `kthread_run` for exactly this.
Combined with a wait queue, the thread can sleep interruptibly and wake on
demand at unload time.

## Mechanism

```
kthread_run(fn, data, name)
  |
  v
fn() loops until kthread_should_stop()
  |
  v
wait_event_interruptible_timeout(wq, stop, delay)
  |                              (sleeps with jitter)
  v
kthread_stop(thread)             (wakes the thread, waits for exit)
```

The thread appears as a normal process in `ps` output.
The name you give it should blend in.
The jittered delay prevents predictable timing patterns that a defender could
fingerprint.

## Your task

Edit `main.c`.
Complete:

1. The thread function `rk_agent_fn`: loop while `!kthread_should_stop()`,
   increment a counter, then sleep using `wait_event_interruptible_timeout` with
   a jittered delay.
2. `rk_init`: start the thread with `kthread_run`.
3. `rk_exit`: stop the thread with `kthread_stop`.

The jittered delay is `base_delay + get_random_u32() % jitter_range`, both
expressed in jiffies.
Use `msecs_to_jiffies()` for the conversion.

## Demo

```
vm$ insmod rklings.ko
vm$ ps aux | grep rkworker
root  ...  [rkworker]
vm$ rmmod rklings
vm$ ps aux | grep rkworker
(nothing)
```

## Verification

```
./rklings check
```
