# Local covert control via mkdir

A rootkit needs a local control channel that works without network access.
This milestone hooks `__x64_sys_mkdir` and parses structured commands from
the path argument. The calling process never creates a real directory: the
hook intercepts the path, executes the command, and returns a result code.

## Command grammar

```text
mkdir rkling.h.m              hide module (list_del)
mkdir rkling.u.m              unhide module (list_add)
mkdir rkling.e                escalate calling process to root
mkdir rkling.h.f.PREFIX       hide file prefix (future: M03)
mkdir rkling.h.i.PID          hide process id (future: M03)
mkdir rkling.h.c.STRING       hide content substring (future: M06)
mkdir rkling.h.p.PORT         hide port number (future: M06)
```

The hook checks whether the path starts with `rkling.`. If it does, parse
the command character and dispatch. Return 0 on success, -ENOENT for
unrecognized commands (indistinguishable from a normal missing directory).

## Privilege escalation

`mkdir rkling.e` grants root credentials to the calling task using
the credential replacement technique from L10. After the syscall
returns, the process has uid 0, gid 0, and full capabilities.

## Your task

In `main.c`, implement:

1. The mkdir kprobe pre-handler that extracts the path from the first
   syscall argument
2. `rk_parse_local_command()` that matches `rkling.` and dispatches:
   - `h.m` calls your module cloak function
   - `u.m` calls your module uncloak function
   - `e` performs credential escalation
   - Anything else returns -ENOENT
3. Init: register the kprobe, create /proc/rkling_status
4. Exit: unregister kprobe, uncloak module, remove proc entry

Run:

```sh
./rootkitlings check
```

## References

- [Kprobes API][1]
- [Linux v7.2 credential replacement][2]
- [Linux v7.2 module list manipulation][3]

[1]: https://docs.kernel.org/7.2/trace/kprobes.html
[2]: https://github.com/torvalds/linux/blob/v7.2/kernel/cred.c
[3]: https://github.com/torvalds/linux/blob/v7.2/include/linux/list.h
