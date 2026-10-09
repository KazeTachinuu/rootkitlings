# Interpret the current task's credentials

Extend `/proc/rootkitlings` with the credentials of the task that materializes the record:

```text
active=1
reader_tid=123
reader_comm="cat"
uids=0 0 0 0
gids=0 0 0 0
cap_userns=initial
cap_permitted=000001ffffffffff
cap_effective=000001ffffffffff
```

The four IDs are ordered as real, effective, saved, and filesystem.
Capability masks are fixed-width, lowercase hexadecimal without a `0x` prefix.
Their exact value depends on the kernel's supported capabilities.

## Take one immutable snapshot

Call `current_cred()` once and keep its `const struct cred *` result local to the show callback.
These are the subjective credentials with which the current task acts.
A published credential is immutable, and the current task cannot replace its credentials while that same thread is executing this callback.
The callback therefore needs neither a lock nor a credential reference.

This rule is specific to immediate access to `current` credentials.
Holding a credential beyond the callback or reading another task's credentials has different lifetime rules.

The ID roles are:

- real: the task's underlying user or group identity
- effective: the identity used by many permission checks
- saved: an identity that a set-ID task can restore
- filesystem: the identity used for filesystem access checks

The primary GID is not the supplementary group list.
This exercise does not render that separate, variable-length state.

## Separate the credential source from the numeric view

`current_cred()` describes the task materializing the record.
It does not describe the task that opened the file.

`seq_user_ns(output)` deliberately supplies a different property: the user namespace stored in `output->file->f_cred`, which is the immutable credential captured when the proc file was opened.
Translate every `kuid_t` and `kgid_t` through that opener namespace with `from_kuid_munged()` and `from_kgid_munged()`.

This matches `/proc/<pid>/status`.
If an ID has no mapping in the opener's user namespace, the `*_munged()` helper emits the configured overflow ID.

An inherited or passed descriptor can therefore combine:

- identity fields from the task materializing the record
- numeric UID/GID coordinates from the task that opened the file

That split is the record contract, not an accident.
The checker passes a proc descriptor from a nested namespace to a reader in the initial namespace and verifies both sides independently.

## Capabilities also have a namespace

Print only the permitted and effective capability sets:

- permitted is the set from which the task may enable capabilities
- effective is the set consulted for current capability checks

Use the same credential snapshot for both.
The in-tree proc implementation uses `seq_put_hex_ll()`, but Linux v7.2 does not export that symbol to external modules.
Render the identical fixed-width form with `seq_printf(..., "%016llx", ...)`
and an explicit `unsigned long long` cast.

`cap_userns` reports where those sets are anchored.
Compare `credentials->user_ns` with `init_user_ns` and print `initial` or `nested`.
A task can have UID 0 and a full effective mask in a nested namespace without having those capabilities over objects owned by the initial namespace.
This record describes authorization context; it is not an authorization decision.

The inheritable, bounding, and ambient sets affect capability transitions across `execve()`.
No mechanism in this exercise performs such a transition, so they are not part of this record.

## Your task

Edit only `interface/proc.c`.

In `rk_status_show()`:

1. take one `current_cred()` snapshot
2. obtain the opener's display namespace with `seq_user_ns()`
3. translate and print all four UID and GID roles
4. identify the credential's capability namespace
5. print the permitted and effective masks

Keep the task attribution, command-name escaping, state access, proc publication, and teardown unchanged.
Do not prepare, mutate, retain, or replace credentials in this exercise.

Run:

```bash
./rootkitlings check
```

The checker compares the record with `/proc/thread-self/status` under initial root, an unprivileged identity, distinct ID roles, a credential change after open, and a nested user namespace.
It also tests a descriptor passed across user namespaces, failure rollback, unload, and two complete load cycles.

## Official Linux v7.2 references

- [Credential model and lifetime](https://docs.kernel.org/7.2/security/credentials.html)
- [`struct cred` and `current_cred()`](https://github.com/torvalds/linux/blob/v7.2/include/linux/cred.h)
- [UID and GID translation](https://github.com/torvalds/linux/blob/v7.2/include/linux/uidgid.h)
- [`seq_user_ns()`](https://github.com/torvalds/linux/blob/v7.2/include/linux/seq_file.h#L248-L255)
- [`/proc/<pid>/status` identity rendering](https://github.com/torvalds/linux/blob/v7.2/fs/proc/array.c#L152-L202)
- [`/proc/<pid>/status` capability rendering](https://github.com/torvalds/linux/blob/v7.2/fs/proc/array.c#L300-L329)
- [Namespace-aware capability checks](https://github.com/torvalds/linux/blob/v7.2/security/commoncap.c#L56-L132)
