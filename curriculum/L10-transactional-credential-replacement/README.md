# Replace the writing task's credentials as one transaction

The proc endpoint accepts `enable`, `disable`, and `root`, each with one
optional final newline.
It starts disabled, so `root` returns `-EACCES` until enabled.
A successful `root` changes only the thread executing that write, not the module
loader, parent shell, another process, or an existing sibling thread.

## Replace one complete credential

Published credentials are immutable.
Prepare one private replacement, then either publish or discard it:

```c
replacement = prepare_kernel_cred(&init_task);
```

Linux v7.2 rejects a `NULL` argument with a warning and `NULL` result.
Pass `&init_task` and return `-ENOMEM` when preparation returns `NULL`.

The result is the complete initial credential: all UID and GID roles are zero;
user accounting and capabilities are anchored in the initial user namespace;
permitted, effective, and bounding sets are full; and inheritable and ambient
sets, supplementary groups, and subscribed keyrings are empty.
It also carries consistent securebits, accounting, and security state.

`commit_creds(replacement)` publishes the object as the writer's objective and
subjective credentials.
It updates related accounting, namespace, dumpability, and notification state,
consumes the prepared reference, and returns zero.
Do not modify, retain, free, or abort the pointer after commit.

If preparation succeeds but publication is canceled, call
`abort_creds(replacement)`.
The supplied cancellation path proves that an unpublished credential leaves the
writer unchanged.

Do not copy the current credential and assign only UID and GID fields.
That would leave related accounting, namespace, groups, capabilities,
securebits, keyrings, and security state inconsistent.
Normal set-ID paths use internal helpers that are not all exported to modules;
`prepare_kernel_cred(&init_task)` is the exported complete transition used here.

## Scope and admission

Capability bits are interpreted relative to their user namespace.
Nested UID 0 with a full effective set does not grant authority over
initial-namespace objects.
This action installs the initial task's credential and its initial namespace
anchor.
The checker derives the full mask from `/proc/sys/kernel/cap_last_cap` and
proves the change with a private `chroot()`.

The proc callback runs in the calling thread.
An external `printf` launched by a shell changes that short-lived process, not
its parent; use the supplied checker to keep and inspect the writer.

`/proc/rootkitlings` uses mode `0666` in the disposable guest.
The action switch is operational control, not identity-based authorization.
One `READ_ONCE()` admits or rejects `root`; a concurrent `disable` prevents
future admission but does not wait for an admitted replacement.

## Your task

Edit only `privilege/credentials.c`.
In `rk_credentials_replace_current()`:

1. prepare one complete credential from `&init_task`
2. return `-ENOMEM` if preparation fails
3. preserve the supplied cancellation branch and its `abort_creds()`
4. commit the replacement and return the commit result

Do not assign individual fields, store the pointer in module state, or mutate a
pointer returned by `current_cred()`.

Run:

```bash
./rootkitlings check
```

The checker proves parsing and gating, preparation failure and cancellation,
complete initial-namespace identity, writer and sibling isolation, concurrent
switch behavior, clean unload, and repeated load cycles.

## Official Linux v7.2 references

- [Credential lifetime and replacement rules][ref-1]
- [`prepare_creds()`, `commit_creds()`, `abort_creds()`, and
  `prepare_kernel_cred()`][ref-2]
- [The complete initial credential][ref-3]
- [`struct cred` fields and const accessors][ref-4]
- [Normal set-ID fixups and user accounting][ref-5]
- [Capability changes during normal set-ID operations][ref-6]
- [Namespace-aware capability checks][ref-7]

[ref-1]:
  https://docs.kernel.org/7.2/security/credentials.html

[ref-2]:
  https://github.com/torvalds/linux/blob/v7.2/kernel/cred.c

[ref-3]:
  https://github.com/torvalds/linux/blob/v7.2/init/init_task.c#L74-L99

[ref-4]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/cred.h

[ref-5]:
  https://github.com/torvalds/linux/blob/v7.2/kernel/sys.c

[ref-6]:
  https://github.com/torvalds/linux/blob/v7.2/security/commoncap.c

[ref-7]:
  https://github.com/torvalds/linux/blob/v7.2/security/commoncap.c#L56-L132
