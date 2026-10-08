# File tripwire

This module records which process attempts to open `/etc/passwd` or `/etc/shadow`.
A kprobe observes `do_sys_openat2()` without changing the original file operation.

## Operate it

```sh
insmod hello.ko
cat /etc/shadow >/dev/null
cat /etc/passwd >/dev/null
cat /proc/rk_tripwire
```

```text
event=1 uid=0 pid=112 path=/etc/shadow
event=2 uid=0 pid=113 path=/etc/passwd
```

Pause collection through the module parameter:

```sh
echo 0 > /sys/module/hello/parameters/armed
cat /etc/shadow >/dev/null
cat /proc/rk_tripwire
```

No event is added while `armed=0`.
Writing `1` resumes collection.

## Probe path

```text
open("/etc/shadow")
        |
        v
do_sys_openat2() -> kprobe pre-handler -> bounded path copy
                                             |
                                             v
                                      watched path?
                                       | yes
                                       v
                                   record event
```

On x86-64, the filename is the function's second argument in `registers->si`.
That value is a userspace pointer and may be invalid.
The provided `copy_user_path()` reads it with `copy_from_user_nofault()` until a null byte or the fixed limit.

The handler always returns zero, so the open continues unchanged.
It does not allocate memory or format proc output.

## Bounded event ring

The module retains the newest 16 events in a fixed ring.
Once full, each new event overwrites the oldest one.

`record_event()` stores the sequence, UID, PID, and exact path while holding `events_lock`.
The proc reader copies the ring under that spinlock and formats the private snapshot after releasing it.
This keeps `seq_printf()`, which may sleep, outside the spinlocked region.

## Source boundaries

```text
main.c       proc and probe acquisition, reverse-order rollback
monitor.c    probe callback, event ring, snapshot reader
tripwire.h   monitor interface
```

## Your work

Complete `watched_path()` in `monitor.c`.
Return true only for exact `/etc/passwd` and `/etc/shadow` paths.

Complete `record_event()` under `events_lock`:

1. Calculate the next ring index.
2. Advance `event_head` when the ring is full; otherwise increment `event_count`.
3. Store `next_sequence`, the current UID, current PID, and path.
4. Increment `next_sequence`.

Convert `current_uid()` in `init_user_ns` with `from_kuid_munged()`.
Do not allocate or format output in the probe callback.

## Verify

```sh
./rootkitlings check C04
```

The check performs real watched-file opens, pauses and resumes collection, exceeds the ring capacity, requires only the newest 16 events, and verifies probe cleanup.

The registered probe remains independently visible in `/sys/kernel/debug/kprobes/list`.
