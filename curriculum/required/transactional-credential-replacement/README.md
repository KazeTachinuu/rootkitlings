# Replace the writing task's credentials as one transaction

The proc endpoint now accepts three exact commands:

```text
enable
disable
root
```

One final newline is optional.
The endpoint starts disabled.
`root` returns `EACCES` while disabled.

A successful `root` write replaces the credentials of the thread executing that write.
It does not change the module loader, the parent shell, another process, or an existing sibling thread.

## Replace one complete credential

Published credentials are immutable.
Create a private replacement, handle failure, then either publish or discard that replacement.

For this mechanism, prepare the complete credential associated with Linux's initial task:

```c
replacement = prepare_kernel_cred(&init_task);
```

Linux v7.2 rejects a `NULL` argument to `prepare_kernel_cred()` with a warning and a `NULL` result.
Use `&init_task` and return `-ENOMEM` if preparation returns `NULL`.

The prepared credential contains one consistent set of related state:

- every real, effective, saved, and filesystem UID and GID is zero
- its user and accounting records describe the initial root user
- its capability namespace is the initial user namespace
- its permitted, effective, and bounding capability sets are full
- its inheritable and ambient capability sets are empty
- its supplementary group list is empty
- its securebits have their default value
- it has no subscribed keyrings
- its security state is prepared from the initial task through the security hook

`commit_creds(replacement)` publishes the new object as both the writing task's objective and subjective credentials.
It also updates kernel accounting, namespace references, dumpability, and change notifications.
It consumes the prepared reference and returns zero.
Do not free, abort, retain, or modify the credential after commit.

If work after preparation fails before publication, call `abort_creds(replacement)` instead.
The supplied cancellation variant exercises that path and proves that an unpublished credential does not change the writer.

## Why not assign eight ID fields

Do not copy the current credential and assign only its UID and GID fields.
That leaves its user accounting, user namespace, supplementary groups, capabilities, securebits, keyrings, and security state based on the old identity.

The normal set-ID implementation repairs this related state with internal helpers such as `set_user()`, `set_cred_ucounts()`, and `security_task_fix_setuid()`.
Those helpers are not all exported to external modules on the pinned kernel.
An external module therefore cannot present eight field assignments as a complete root credential transition.

`prepare_kernel_cred(&init_task)` is an exported kernel-service API that returns the complete initial credential.
Committing that object to a userspace writer is the rootkit mechanism taught here, not a normal driver identity change.

## User namespaces change the meaning of privilege

Capability bits are interpreted relative to the credential's user namespace.
UID 0 with a full effective set inside a nested user namespace does not grant authority over objects governed by the initial user namespace.

This action installs the initial task's complete credential, including its initial-user-namespace anchor.
It is stronger than changing a displayed UID to zero in the caller's existing namespace.

The checker derives the full capability mask from `/proc/sys/kernel/cap_last_cap` rather than embedding a version-specific constant.
It also verifies a private `chroot()` that fails before replacement and succeeds afterward.

## The writer is the task that changes

The proc write callback runs in the thread that called `write(2)`.
`commit_creds()` changes that task.

If a shell launches an external `printf`, that short-lived `printf` process performs the write and then exits.
Its parent shell does not become privileged.
Use the supplied checker to keep the writer alive and inspect that exact task after the write.

After replacement, `/proc/rootkitlings` reports the writer's new state when the same thread reopens and reads it:

```text
active=1
actions=enabled
reader_tid=123
reader_comm="cred-trigger"
uids=0 0 0 0
gids=0 0 0 0
cap_userns=initial
cap_permitted=000001ffffffffff
cap_effective=000001ffffffffff
```

The example mask is only illustrative.
The running kernel determines the exact full mask.

## The switch is not authorization

`/proc/rootkitlings` is mode `0666` inside the disposable guest so an unprivileged task can exercise the write callback directly.
The `enable` and `disable` commands are an operational switch, not an identity check.
Any task that can write the endpoint can change that switch.

One `READ_ONCE()` decides whether `root` is admitted.
An admitted replacement may complete while another task disables future actions.
`disable` is not a barrier that waits for an admitted write.

## Your task

Edit only `privilege/credentials.c`.

In `rk_credentials_replace_current()`:

1. prepare one complete credential from `&init_task`
2. return `-ENOMEM` if preparation fails
3. preserve the supplied cancellation branch and its `abort_creds()` call
4. commit the prepared credential and return the commit result

Do not assign individual credential fields.
Do not store the prepared pointer in module state.
Do not mutate a pointer returned by `current_cred()`.

Run:

```bash
./rootkitlings check
```

The checker covers exact parsing, disabled and malformed commands, preparation failure, cancellation, complete identity and capability state, initial-user-namespace scope, task and thread isolation, concurrent enable and disable, unload, and two load cycles.

## Official Linux v7.2 references

- [Credential lifetime and replacement rules](https://docs.kernel.org/7.2/security/credentials.html)
- [`prepare_creds()`, `commit_creds()`, `abort_creds()`, and `prepare_kernel_cred()`](https://github.com/torvalds/linux/blob/v7.2/kernel/cred.c)
- [The complete initial credential](https://github.com/torvalds/linux/blob/v7.2/init/init_task.c#L74-L99)
- [`struct cred` fields and const accessors](https://github.com/torvalds/linux/blob/v7.2/include/linux/cred.h)
- [Normal set-ID fixups and user accounting](https://github.com/torvalds/linux/blob/v7.2/kernel/sys.c)
- [Capability changes during normal set-ID operations](https://github.com/torvalds/linux/blob/v7.2/security/commoncap.c)
- [Namespace-aware capability checks](https://github.com/torvalds/linux/blob/v7.2/security/commoncap.c#L56-L132)
