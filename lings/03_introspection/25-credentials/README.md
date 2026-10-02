# 24 - Credentials

## Why this step

`current->pid` tells you who. `current_cred()` tells you what they can do.
The credential structure holds uid, gid, capabilities. A rootkit reads
these to decide whether to obey a command.

## What you need

You already know `current` from ling 23. This ling reads the caller's
credentials using `current_cred()`.

New includes:

```c
#include <linux/cred.h>    // current_cred, struct cred
#include <linux/uidgid.h>  // from_kuid, from_kgid, init_user_ns
```

## The concept

```
  cat /proc/rk_creds
    |
    v
  rk_read()
    |
    v
  current_cred()  -->  struct cred
                         |
                         +-> uid   (kuid_t, not a plain int)
                         +-> gid   (kgid_t, not a plain int)
                         +-> euid, egid, suid, sgid ...
                         +-> cap_effective, cap_permitted ...
```

Kernel UIDs are namespace-aware types (`kuid_t`), not plain integers.
To print them, translate through the init namespace:

```c
from_kuid(&init_user_ns, cred->uid)  // returns uid_t (unsigned int)
```

## Build it

Create a `/proc/rk_creds` entry (same pattern as ling 10) whose read
callback:

1. Calls `current_cred()` to get the caller's credentials.
2. Formats `uid=N gid=N` using `from_kuid` and `from_kgid`.
3. Returns the string with `seq_printf` via `proc_create_single`.

`current_cred()` returns a `const` pointer. You cannot modify
credentials through it. That comes in ling 25.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_creds       # uid=0 gid=0 (running as root)
```

## Contract

The check script verifies:

- `/proc/rk_creds` must output lines in this format:
  ```
  uid=N gid=N
  ```
- `/proc/rk_creds` must contain `uid=` and `gid=` from the calling process's credentials.

## Verify

```bash
./rootkitlings check 22
```

> **What a defender sees**: the module reads credentials but does not
> change them. Reading is invisible. Changing them (ling 25) is not.

See also: [23 - Credential escalation](../23-credential-escalation/README.md).
