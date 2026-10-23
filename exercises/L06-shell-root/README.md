# L06: Give the Calling Shell Root

## Mission

L05 recognizes a command name but only logs it. Make `mkdir rklings.e` give root
privileges to the shell that ran it.

No command directory should be created, and every other name must still behave
like an ordinary `mkdir` request.

## What you will observe

```console
student@victim:~$ id
uid=1000(student) gid=1000(student) groups=1000(student)
student@victim:~$ mkdir rklings.e
student@victim:~$ id
uid=0(root) gid=0(root) groups=0(root)
student@victim:~$ ls rklings.e
```

`ls` finds no command directory because the hook consumed the request.

Another name remains an ordinary directory and does not change the shell:

```console
student@victim:~$ mkdir rklings.other
student@victim:~$ id
uid=1000(student) gid=1000(student) groups=1000(student)
student@victim:~$ ls -d rklings.other
rklings.other
```

## Workspace

```text
exercises/L06-shell-root
├── Kbuild
├── main.c
├── rklings_creds.h
└── rklings_hook.h
```

Only `main.c` is editable.
You connect `rklings.e` to the shell that ran `mkdir`.

Supplied files:

- `rklings_hook.h` recognizes `rklings.e`.
- `rklings_creds.h` gives root privileges to a selected process ID.

## Follow the parent and child

Credentials are the kernel state that records a process's user and group
identity and permissions.
Giving the shell root credentials makes later permission checks treat it as
root.

The shell does not create the directory itself.
It starts a child process that runs `mkdir`:

```text
shell
PID 1200, UID 1000
    |
    └── starts mkdir
            PID 1234, UID 1000
                |
                └── enters the kernel
                        current = PID 1234
                        parent PID = 1200
                                |
                                └── give PID 1200 root credentials
    |
    ├── child exits
    └── shell continues as UID 0
```

Inside the hook, `current` is the `mkdir` child. `task_ppid_nr(current)` returns
its parent PID, which identifies the shell.
Changing the child would be useless because it exits as soon as `mkdir`
finishes; changing the parent leaves the interactive shell running as root.

## Turn the command into privilege

The supplied hook calls your callback only for the exact `rklings.e` command.
Pass the parent PID to `rk_escalate_pid` and preserve any error it returns.

After success, log the changed PID and return `0`.

## TODOs

```text
TODO 1  main.c:11:2  elevate the parent shell for the exact command
```

### TODO 1 -- `main.c:11:2` -- Elevate the calling shell

```text
rklings.e  -> elevate its parent shell; create no directory
other name -> continue normal mkdir
```

1. Store `task_ppid_nr(current)` in a `pid_t parent_pid`. `pid_t` is the
   kernel's integer type for a process ID.
2. Pass that PID to `rk_escalate_pid` and return its error if it fails.
3. Log `rklings: escalated pid %d to root\n` with `parent_pid`.
4. Return `0` so the command succeeds without creating `rklings.e`.

## Test it

From the host, build the exercise and open the student shell:

```console
$ ./rklings run L06
```

Inside the guest, run the commands from **What you will observe**.

## Validate


```console
$ ./rklings validate L06
  [+] Exact command gives its calling shell root credentials
  [+] Other names keep their normal behavior
  PASS
```


## Next

L06 always targets the parent shell.
L07 opens the credential helper and extends the command with an explicit PID so
it can target another running process.
