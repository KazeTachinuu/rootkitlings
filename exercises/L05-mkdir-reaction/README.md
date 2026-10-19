# L05: Intercept a `mkdir` Call

L04 accepted commands through a visible procfs file.
This exercise recognizes a directory name while the kernel is handling `mkdir`,
logs its suffix, and prevents the directory from being created.

## What you will observe

```console
root@victim:~# mkdir rklings.hello
mkdir: cannot create directory 'rklings.hello': No such file or directory
root@victim:~# test ! -e rklings.hello && echo absent
absent
root@victim:~# dmesg | tail -1
rklings: intercepted mkdir: hello
```

A normal name must retain normal filesystem behavior:

```console
root@victim:~# mkdir ordinary
root@victim:~# test -d ordinary && echo created
created
```

## Workspace

The workspace contains three files:

```text
exercises/L05-mkdir-reaction
├── Kbuild
├── main.c
└── rklings_hook.h
```

Only `main.c` is editable.
Read the call path below before completing its callback TODO.

## Follow the command into the kernel

The shell does not implement directory creation itself.
It asks the kernel through the `mkdir` system call:

```text
SHELL                              KERNEL
mkdir rklings.hello
        |
        +-- mkdir("rklings.hello") ---> interception layer
                                                |
                                                +-- extract basename
                                                v
                                         RK_ON_MKDIR(name)
```

The supplied layer passes the basename to your callback.
For `/tmp/rklings.hello`, the callback receives `rklings.hello`, not the entire
path.

```c
RK_ON_MKDIR(name)
{
	/* name points to the basename */
}
```

## Separate matching names from ordinary names

The command prefix is `rklings.`.
Compare only that prefix first:

```text
name                    prefix matches?      action
--------------------    ---------------      ---------------------
rklings.hello           yes                  log "hello"
rklings.status          yes                  log "status"
ordinary                no                   do not log
rklings                 no                   do not log
```

After a match, advance past the prefix to print only the suffix:

```text
rklings.hello
+-------++---+---+
 prefix     suffix
 8 bytes    name + 8
```

Use `strncmp` for the bounded prefix comparison.
Do not use `strstr`: a name such as `ordinary-rklings.hello` is not a command.

## Understand suppression

The callback's return value controls what happens next:

```text
matching name                         ordinary name
-------------                         -------------
callback returns -ENOENT              callback returns 0
        |                                     |
        v                                     v
mkdir reports failure                 mkdir reaches filesystem
directory is absent                   directory is created
```

`ENOENT` means “no such file or directory.” Userspace receives its negative
kernel form, `-ENOENT`, which the C library turns into the familiar error
message.

This lesson intentionally returns that error for intercepted names.
L09 later replaces the final syscall result after a command succeeds.

## Own the hook lifetime

The header exposes:

```c
int rk_hook_start(void);
void rk_hook_stop(void);
```

`rk_hook_start` installs the interception and may fail.
The supplied `rk_init` returns that exact error, and the supplied `rk_exit`
stops the hook before the module code disappears.
Read this lifecycle, but do not rewrite it in this exercise.

## Implement it

In `main.c`:

1. In `RK_ON_MKDIR`, compare `name` with the exact `rklings.` prefix.
2. For a match, log `rklings: intercepted mkdir: <suffix>`.
3. Do not log ordinary names.
4. Return `0` for ordinary names and `-ENOENT` for intercepted names.

Do not modify `rklings_hook.h`.

## Test it

Build the module and enter the VM:

```console
$ ./rklings run L05
```

Load it, then compare a matching and non-matching name:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# mkdir rklings.hello
mkdir: cannot create directory 'rklings.hello': No such file or directory
root@victim:~# dmesg | tail -1
rklings: intercepted mkdir: hello
root@victim:~# mkdir ordinary
root@victim:~# test -d ordinary && echo created
created
root@victim:~# rmdir ordinary
```

Confirm that `rklings.hello` was never created:

```console
root@victim:~# test ! -e rklings.hello && echo absent
absent
```

## Validate

From the repository root:

```console
$ ./rklings validate L05
```

The checks exercise the matching log, suppressed directory, ordinary directory
creation, absence of a false log, and clean module lifetime.

## Next

The intercepted name currently produces only a kernel log entry.
L06 maps `rklings.e` to credential replacement so the shell that issued the
command becomes root.
