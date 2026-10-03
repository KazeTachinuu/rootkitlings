# 56 - Self-healing

## What we are learning

Advanced rootkits monitor their own state and repair damage.

If a defender removes a `/proc` entry, deletes a hidden file, or kills
a backdoor thread, the rootkit's watchdog detects the loss and recreates
it. Manual cleanup becomes insufficient: you must remove the module
itself.

```
  defender                  watchdog kthread
  --------                  ----------------
  rm /proc/rk_watchdog            sleeping...
     |                         |
     v                    check: entry == NULL?
  entry gone                   |
                          yes --> recreate
                               |
                          /proc/rk_watchdog is back
                          heals++
```

## What you already know

From ling 19: kthread loops with `kthread_should_stop`. From ling 12:
proc entry creation. This ling combines them into a self-repairing
system.

## Build it

1. Create `/proc/rk_watchdog` with `0666` permissions.
2. The write handler accepts `"kill"` and sets an atomic request flag. It must
   not remove the proc entry from inside its own active callback.
3. Spawn a watchdog kthread. It consumes the request, removes the entry after
   the writer has returned, recreates it, and increments a heal counter.
4. `/proc/rk_watchdog` serves the heal count and watchdog status.
5. In `exit`, stop the watchdog before removing the proc entry.

Use `schedule_timeout_interruptible(HZ)` for the one-second sleep
(interruptible so `kthread_stop` can wake it).

Only the watchdog changes the entry pointer while the module is live. Module
exit first stops that thread, then removes the final entry. The write callback
shares only the atomic request flag with the watchdog.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_watchdog                 # heals=0, watchdog=active
printf 'kill' > /proc/rk_watchdog     # destroy the proc entry
ls /proc/ | grep rk_watchdog    # gone momentarily
sleep 5
ls /proc/ | grep rk_watchdog    # reappeared (self-healed)
cat /proc/rk_watchdog                 # heals=1
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_watchdog` contains `watchdog=active` and `heals=N`.
- Writing `kill` destroys the proc entry.
- A watchdog thread recreates `/proc/rk_watchdog` within a few seconds.
- `heals` increments each time the watchdog recreates the entry.

## Verify

```bash
./rootkitlings check 56
```

The check writes `kill`, then polls for a recreated entry with an incremented
counter until a bounded timeout expires.

> **What a defender sees**: a kernel thread named `kheal` is visible in
> `ps`. The proc entry reappears after removal. Unloading or disabling the
> responsible module stops this implementation.

See also: [Crypto](../../08_crypto/README.md).
