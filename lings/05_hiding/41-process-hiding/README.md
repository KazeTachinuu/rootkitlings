# 41 - Process hiding

## Why this step

`ps` lists processes by iterating `/proc/[pid]/`. Hook getdents64
on `/proc` and filter numeric entries matching a target PID. The
process runs but is invisible to `ps`, `top`, and `htop`. This is
how Diamorphine and Reptile hide their backdoor processes.

## What you already know

From ling 19, you created kthreads. From ling 38, you hooked
getdents64 with a kretprobe to hide files. This ling combines both:
spawn a thread, then hide its PID from the `/proc` listing.

## The concept

```
  ps / top / htop
       |
       v
  ls /proc  (getdents64)
       |
       v
  +------+------+------+------+------+
  |  1   | 127  | 482  | 513  | 901  |    <-- PID directories
  +------+------+------+------+------+
                   ^
                   hidden PID = 482
       |
       v  (your kretprobe filters here)

  +------+------+------+------+
  |  1   | 127  | 513  | 901  |           <-- PID 482 removed
  +------+------+------+------+

  ps sees: init(1), bash(127), sshd(513), login(901)
  ps does NOT see: khidden(482)
  but: kill -0 482 still returns 0 (process exists)
```

The hook is identical to ling 38, but the filter checks numeric
`d_name` values against your hidden PID instead of a string prefix.

## Build it

Create a module that:

1. Spawns a kthread named `"khidden"` (from ling 19).
2. Stores its PID as a string (`snprintf` into a char array).
3. Registers a kretprobe on `__x64_sys_getdents64` (same entry+return
   pattern as ling 38).
4. In the return handler, filters entries whose `d_name` matches the
   PID string exactly (`strcmp` instead of `strncmp`).
5. Exposes the hidden PID via `/proc/rk_ghost` as a plain number.

You need the same includes as ling 38: `linux/types.h` before
`linux/dirent.h`, plus `linux/kthread.h` for the thread.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_ghost                 # shows hidden PID info
ls /proc/ | wc -l               # count entries
sleep 999 &
ls /proc/ | wc -l               # hidden PID not counted
kill %1
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_ghost` returns the hidden PID as a plain number.
- That PID does not appear in `ps` output.
- Direct access to `/proc/PID` still works.
- `/proc/rk_ghost` disappears after unload.

## Verify

```bash
./rootkitlings check 41
```

> **What a defender sees**: PID gaps in `/proc`. A `kill -0` sweep
> on all PIDs up to `pid_max` finds running processes that `ps`
> does not show.

See also: [42 - Port hiding](../42-port-hiding/README.md).
