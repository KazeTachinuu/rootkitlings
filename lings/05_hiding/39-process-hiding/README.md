# 39 - Process hiding

## Why this step

`ps` lists processes by iterating `/proc/[pid]/`. Hook getdents64
on `/proc` and filter numeric entries matching a target PID. The
process runs but is invisible to `ps`, `top`, and `htop`. This is
how Diamorphine and Reptile hide their backdoor processes.

## What you already know

From ling 18, you created kthreads. From ling 36, you hooked
getdents64 to hide files. This ling combines both: spawn a thread,
then hide its PID from the `/proc` listing.

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
       v  (your hook filters here)

  +------+------+------+------+
  |  1   | 127  | 513  | 901  |           <-- PID 482 removed
  +------+------+------+------+

  ps sees: init(1), bash(127), sshd(513), login(901)
  ps does NOT see: khidden(482)
  but: kill -0 482 still returns 0 (process exists)
```

The hook is identical to ling 36, but the filter checks numeric
`d_name` values against your hidden PID instead of a string prefix.

## Build it

Create a module that:

1. Spawns a kthread (from ling 18).
2. Hooks `__x64_sys_getdents64` (same ftrace pattern as ling 36).
3. In the wrapper, only filter when listing `/proc` (check dentry name).
4. Hide entries whose `d_name` matches your kthread's PID.
5. Exposes the hidden PID via `/proc/rk_ghost`.

The filter logic: for entries under `/proc`, check if the name is
numeric and matches your hidden PID. Skip matching entries.

## Hints

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
./rootkitlings check 37
```

> **What a defender sees**: PID gaps in `/proc`. A `kill -0` sweep
> on all PIDs up to `pid_max` finds running processes that `ps`
> does not show.

See also: [38 - Port hiding](../38-port-hiding/README.md).
