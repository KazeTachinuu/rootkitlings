# 37 - Process hiding

## Why this step

`ps` lists processes by iterating `/proc/[pid]/`. Hook getdents64
on `/proc` and filter numeric entries matching a target PID. The
process runs but is invisible to `ps`, `top`, and `htop`. This is
how Diamorphine and Reptile hide their backdoor processes.

## What you already know

From ling 16, you created kthreads. From ling 34, you hooked
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

The hook is identical to ling 34, but the filter checks numeric
`d_name` values against your hidden PID instead of a string prefix.

## Build it

Create a module that:

1. Spawns a kthread (from ling 16).
2. Hooks `__x64_sys_getdents64` (same ftrace pattern as ling 34).
3. In the wrapper, only filter when listing `/proc` (check dentry name).
4. Hide entries whose `d_name` matches your kthread's PID.
5. Exposes the hidden PID via `/proc/rk_ghost`.

The filter logic: for entries under `/proc`, check if the name is
numeric and matches your hidden PID. Skip matching entries.

## Predict

- Does `kill -0 <pid>` still work on a hidden process?
- Can you hide arbitrary user processes, not just kthreads?
- What about `/proc/[pid]/` subdirectories?


## Hints

<details>
<summary>Hint 1: reuse the getdents64 hook from ling 34</summary>

The hook structure is identical to ling 34. The only change is the
filter condition: instead of matching a filename prefix, convert each
`d_name` to a number with `kstrtoint` and compare against your hidden
PID.

</details>

<details>
<summary>Hint 2: which directory to filter</summary>

You only want to filter entries when `getdents64` is listing `/proc`,
not arbitrary directories. Check the file descriptor's path: if
it points to `/proc` (its dentry name is `"proc"`), apply the filter.
Otherwise, pass through.

</details>

<details>
<summary>Hint 3: getting your kthread's PID</summary>

`kthread_run` returns a `struct task_struct *`. The PID is
`task->pid`. Store it globally so the getdents64 hook can compare
against it, and so `/proc/rk_ghost` can report it.

</details>

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
