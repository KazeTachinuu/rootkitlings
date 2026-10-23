# L14: Usermode Helper

## Mission

Add a `launch` control that starts an absolute executable path as root from the
kernel. You will prove it by launching a script that creates `/tmp/proof`.

M01 gave you a complete local rootkit, but:

- Every command requires typing mkdir in a shell on the target
- That means you need shell access first
- You type every command by hand

The kernel can launch programs on its own. If the kernel can start an agent
binary, you never need to type another command.

## What you will observe

Creating `rklings.launch.<absolute-path>` returns through the intercepted mkdir
call while a kernel work item starts that path as a separate root process. The
proof file appears only when the userspace program actually executes; existing
ping and escalation controls must continue to work.

## Workspace

The exercise carries the M01 module forward and adds one editable component:

```text
exercises/L14-usermode-helper
├── core
│   ├── dispatch.c
│   ├── escalation.c
│   ├── filter.c
│   ├── init.c
│   ├── launch.c
│   ├── module.c
│   └── target.c
├── include
│   └── rootkit.h
└── Kbuild
```

The dispatcher and existing controls are provided. Implement only the launch
handler and its registration.

## call_usermodehelper

The kernel uses `call_usermodehelper` to run userspace programs. It is the same
mechanism the kernel uses to load firmware and run modprobe.

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

- `UMH_WAIT_EXEC`: return after exec (not after the process exits). The process
  runs in the background.
- `UMH_WAIT_PROC`: block until the process exits. Never use this in a syscall
  handler: it deadlocks.
- `UMH_NO_WAIT`: fire and forget. No error reporting if exec fails.

`UMH_WAIT_EXEC` is the right choice. It confirms the binary exists and is
executable, then returns.

## argv and envp

Both are NULL-terminated string arrays, just like execve. The first element of
argv is the program name (convention: same as path).

```c
char *argv[] = { "/tmp/agent", NULL };
char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/bin", NULL };
```

The environment must include PATH or the process inherits an empty environment.
Minimal is fine: HOME and PATH.

## The mkdir command

`rklings.launch.<path>` triggers the launch:

- The argument after "launch." is the absolute path to the binary
- Dots in the path work because the dispatcher uses longest-prefix matching:
  "launch" matches, everything after the first dot is the argument

```text
rklings.launch./tmp/agent    -> launch "/tmp/agent"
rklings.launch./bin/echo     -> launch "/bin/echo"
```

## TODOs

```text
TODO 1  core/launch.c:12:1  validate the path argument
TODO 2  core/launch.c:13:1  build argv and envp, launch the binary
```

### TODO 1 -- `core/launch.c:12:1` -- Validate the path argument

Reject the call if `arg` is NULL or does not start with `"/"`. Return `-EINVAL`
for an invalid path.

### TODO 2 -- `core/launch.c:13:1` -- Build argv and envp, launch the binary

1. Construct a NULL-terminated `argv` with the path as its only element
2. Construct a minimal `envp` with at least `HOME` and `PATH`
3. Call `call_usermodehelper` with `UMH_WAIT_EXEC` and return its result

## Test it

```console
$ ./rklings run L14
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# cat > /tmp/test.sh << 'EOF'
#!/bin/sh
echo launched > /tmp/proof
EOF
root@victim:~# chmod +x /tmp/test.sh
root@victim:~# mkdir rklings.launch./tmp/test.sh
root@victim:~# cat /tmp/proof
launched
root@victim:~# dmesg | tail -2
rklings: launch: /tmp/test.sh (ret=0)
```

- The process runs as root (kernel context)
- Its parent is kthreadd (PID 2), not the shell
- A short script like this exits instantly; you will see it in dmesg, not in
  `ps`

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
operator yet. G00 qualifies the Go and stream-I/O mechanics used by that
program. L15 then builds an authenticated callback so the target can connect to
your server.
