# L05: Turn `mkdir` into a Command Trigger

## Mission

L04 sends commands through `/proc/rklings`, an obvious control file. `mkdir` is
a normal filesystem operation that leaves no trace in logs. No monitoring tool
flags directory creation as suspicious.

Carry a command through an ordinary `mkdir` call. Treat names beginning with
`rklings.` as commands and leave every other directory request unchanged.

## What you will observe

A matching name is treated as a command rather than a directory:

```console
root@victim:~# mkdir rklings.hello
root@victim:~# ls rklings.hello
root@victim:~# dmesg | tail -1
rklings: intercepted mkdir: hello
```

`ls` finds no command directory because the hook consumed the request.

An ordinary name keeps its normal meaning:

```console
root@victim:~# mkdir ordinary
root@victim:~# ls -d ordinary
ordinary
```

## Workspace

```text
exercises/L05-mkdir-command
├── Kbuild
├── main.c
└── rklings_hook.h
```

Only `main.c` is editable. You decide whether one directory name is a command.

`rklings_hook.h` supplies the code that intercepts `mkdir` and checks the
prefix.

## Follow `mkdir` into the callback

The `mkdir` command asks the kernel to create a directory through a system call.
The supplied interception layer catches that request and passes the name to
`RK_ON_MKDIR`:

```text
mkdir rklings.hello
          |
          └── system call enters the kernel
                         |
                         ├── supplied layer copies the pathname
                         └── supplied layer checks the prefix
                                             |
                                             v
                                  RK_ON_MKDIR("rklings.hello")
```

Your callback never reads the userspace pathname directly. It receives a safe
kernel copy of the name.

## Split the prefix from the suffix

`strncmp(name, RK_PREFIX, RK_PREFIX_LEN)` compares only the first eight bytes of
`name` with `rklings.`. It returns `0` when those bytes match and a nonzero
value otherwise.

After a match, `name + RK_PREFIX_LEN` points at the first byte after the prefix:

```text
r k l i n g s . h e l l o
| 8-byte prefix| suffix |
                ^
                name + RK_PREFIX_LEN
```

A bounded prefix comparison matters here. `ordinary-rklings.hello` contains the
command text, but it does not begin with it and must remain an ordinary
directory name.

## Decide whether to consume the call

The callback returns a boolean decision to the supplied layer:

| Callback result | Effect                                      |
| --------------- | ------------------------------------------- |
| `false`         | The original `mkdir` continues.             |
| `true`          | The command succeeds; no directory is made. |

For `true`, the supplied entry handler gives the real syscall an empty pathname.
Its return handler then replaces the internal error with `0`, so userspace sees
success without a directory being created.

## TODOs

```text
TODO 1  main.c:13:2  recognize the command prefix and log its suffix
```

### TODO 1 -- `main.c:13:2` -- Recognize the command prefix

| Name                     | Log                                 | Return  |
| ------------------------ | ----------------------------------- | ------- |
| `ordinary`               | nothing                             | `false` |
| `rklings`                | nothing                             | `false` |
| `ordinary-rklings.hello` | nothing                             | `false` |
| `rklings.hello`          | `rklings: intercepted mkdir: hello` | `true`  |

1. Compare `name` with `RK_PREFIX` for exactly `RK_PREFIX_LEN` bytes.
2. Return `false` immediately when the prefix does not match.
3. For a match, log `rklings: intercepted mkdir: %s\n` with `name +
   RK_PREFIX_LEN` as the `%s` argument.
4. Return `true` so the call succeeds without creating the matching directory.

## Test it

From the repository root, build the module and enter the VM:

```console
$ ./rklings run L05
```

Inside the VM, load the module and try one matching and one ordinary name:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# mkdir rklings.hello
root@victim:~# dmesg | tail -1
rklings: intercepted mkdir: hello
root@victim:~# mkdir ordinary
root@victim:~# ls -d ordinary
ordinary
```

## Validate


```console
$ ./rklings validate L05
  [+] mkdir rklings.hello succeeds without creating a directory
  [+] Non-matching mkdir is ignored
  PASS
```


## Next

The intercepted name currently produces only a log entry. L06 maps the exact
name `rklings.e` to a credential change in the parent shell.
