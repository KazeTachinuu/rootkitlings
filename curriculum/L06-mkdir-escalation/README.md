# Become root with a single mkdir

You already know how to intercept a mkdir from the kernel.
Now you will use that interception to do something dangerous: replace the
calling process's credentials with root credentials.

## The credential API

The kernel tracks every process's identity through a `struct cred`.
Two functions let you replace it:

```
struct cred *new = prepare_kernel_cred(&init_task);
commit_creds(new);
```

`prepare_kernel_cred(&init_task)` allocates a new credential structure copied
from the init process (PID 1, which runs as root).
`commit_creds(new)` installs those credentials on the calling process.

After `commit_creds` returns, the process that triggered the mkdir is running as
uid=0.

## What to build

Extend your RK_ON_MKDIR callback:
- If `name` matches `"rklings.e"`, call `prepare_kernel_cred` and `commit_creds`
  to escalate the calling process to root
- Still return `-ENOENT` to prevent the actual directory from being created
- Non-matching names return `-ENOENT` without escalating

You will need two additional headers: `<linux/cred.h>` for the credential
functions and `<linux/sched/task.h>` for `init_task`.

## Demo

```
$ id
uid=1000(user) gid=1000(user) ...
$ mkdir rklings.e
mkdir: cannot create directory 'rklings.e': No such file or directory
$ id
uid=0(root) gid=0(root) ...
```

## Verify

```
./rklings check
```

The test loads your module, runs `mkdir rklings.e` as a normal user, then checks
that the process has uid=0.
It also verifies that a non-matching mkdir does not change credentials.
