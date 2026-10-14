# Local covert control via mkdir

A rootkit needs a local control channel that works without network access.
This milestone hooks `__x64_sys_mkdir` and parses structured commands from the
path argument.
The calling process never creates a real directory: the hook intercepts the
path, executes the command, and returns a result code.

## Command grammar

```text
mkdir rklings.h.m              hide module (list_del)
mkdir rklings.u.m              unhide module (list_add)
mkdir rklings.e                escalate calling process to root
mkdir rklings.h.f.PREFIX       hide file prefix (future: M03)
mkdir rklings.h.i.PID          hide process id (future: M03)
mkdir rklings.h.c.STRING       hide content substring (future: M06)
mkdir rklings.h.p.PORT         hide port number (future: M06)
```

```text
userspace                          kernelspace
                                   __x64_sys_mkdir(uregs)
  mkdir rklings.e                        |
       |                           kprobe fires
       v                                |
  syscall                          uregs->di = pathname
       |                                |
       v                           strncpy_from_user
  return value  <---+                   |
  (0 or -ENOENT)   |              "rklings." prefix?
                    |                   |
                    |              yes: dispatch command
                    |                   |
                    +---- override_function_with_return
                         (skip real mkdir, return result)
```

Return 0 on success, -ENOENT for unrecognized commands (indistinguishable from a
normal missing directory).

## Privilege escalation

`mkdir rklings.e` grants root credentials to the calling task and its parent
shell.
The technique:

1. Allocate a new credential set with `prepare_kernel_cred(&init_task)`.
   This copies init's credentials (uid 0, gid 0, all capabilities).
2. Apply them to the current task with `commit_creds(new_cred)`.
3. Escalate the parent process (`current->real_parent`) too, so the shell that
   ran mkdir also becomes root.
   For the parent path:
   - Check `parent->mm` first.
     Kernel threads have `mm == NULL`;
     escalating them is meaningless and dangerous.
   - Allocate a second credential set.
   - Replace `task->real_cred` and `task->cred` via `rcu_assign_pointer`.
   - Call `put_cred()` on the old credentials to avoid a memory leak.

## Kprobe pre-handler

The kprobe hooks `__x64_sys_mkdir(const struct pt_regs *uregs)`.
The outer `regs` passed to your handler wraps the real syscall registers:

1. Get the inner pt_regs: `(struct pt_regs *)regs_get_kernel_argument(regs, 0)`
2. Read the user pathname pointer from `uregs->di`
3. Copy to kernel buffer with `strncpy_from_user`
4. Check for the `rklings.`
   prefix.
   If not ours, return 0 (let mkdir proceed).
5. Dispatch to `rk_parse_local_command`.
   Set the return value and call `override_function_with_return` to skip the
   real mkdir.
   Return 1.

## Your task

In `main.c`, implement:

1. `rk_escalate()` (credential escalation for current + parent)
2. `rk_parse_local_command()` (command dispatch)
3. `rk_mkdir_pre()` (kprobe pre-handler)
4. `rk_init()` / `rk_exit()`

Run:

```sh
./rklings check
```

## References

- [Kprobes API][1]
- [Linux v7.2 credential replacement][2]
- [Linux v7.2 module list manipulation][3]

[1]: https://docs.kernel.org/7.2/trace/kprobes.html
[2]: https://github.com/torvalds/linux/blob/v7.2/kernel/cred.c
[3]: https://github.com/torvalds/linux/blob/v7.2/include/linux/list.h
