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
student@victim:~$ sleep 30 &
[1] 1234
student@victim:~$ grep '^Uid:' /proc/1234/status
Uid:    1000    1000    1000    1000
student@victim:~$ mkdir rklings.e.1234
student@victim:~$ grep '^Uid:' /proc/1234/status
Uid:    0       0       0       0
```

No `rklings.e.1234` directory is created.
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
RCU protects the lookup; a task reference keeps the result allocated afterward:

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

`cred` is used for permission checks; `real_cred` is visible through `/proc`.
The new object needs two references because both pointers own it.
After the swap, release both old references.

Normal kernel code uses `commit_creds()` to change `current`.
Here, `current` is the short-lived `mkdir` child.
This rootkit changes the selected task directly so the real `mkdir` command can
elevate its parent shell or another PID.

## TODOs

```text
TODO 1  main.c:11:1  replace one userspace task's credentials
```

### TODO 1 — `main.c:11:1` — Replace one task's credentials

Implement `rk_escalate_pid` in this order:

1. Under `rcu_read_lock`, resolve `pid` with
   `pid_task(find_vpid(pid), PIDTYPE_PID)`.
   Return `-ESRCH` if it has no task.
2. Call `get_task_struct(task)` before releasing RCU.
3. Reject `task->mm == NULL` with `-EINVAL`, releasing the task first.
4. Create root credentials with `prepare_kernel_cred(&init_task)`.
   On failure, release the task and return `-ENOMEM`.
5. Under `task_lock`, save `cred` and `real_cred`, add the second new credential
   reference with `get_cred`, and publish the new object through both pointers
   with `rcu_assign_pointer`.
6. Unlock, release both old credentials and the task reference, then return `0`.

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

From the repository root:

```console
$ ./rklings validate L07
```

The validator checks parent-shell escalation, explicit PID escalation through
`/proc/<pid>/status`, malformed commands, ordinary `mkdir`, and clean unload.

## Next

Credential replacement now works, but a supplied header still owns syscall
interception.
L08 opens that header and implements its `mkdir` and `mkdirat` probes directly.
