# Read the caller's kernel identity

A thread name and ID do not explain what that thread may do.
Snapshot the reader's kernel identity: user IDs, group IDs, capabilities, and
the user namespace in which those capabilities have meaning.
Success proves that the record describes one consistent caller while rendering
its numeric IDs for the process that opened the channel.

Linux stores this identity and authority in an immutable **credential** object.
Extend `/proc/rootkitlings` with that snapshot:

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

The IDs are ordered as real, effective, saved, and filesystem.
Capability masks are fixed-width, lowercase hexadecimal without a `0x` prefix.
Their exact value depends on the kernel's supported capabilities.

## Take one immutable snapshot

Call `current_cred()` once and keep its `const struct cred *` result local to
the show callback.
These immutable subjective credentials govern how the current task acts.
The same thread cannot replace them while executing this callback, so immediate
access needs neither a lock nor a credential reference.

This rule is specific to immediate access to `current` credentials.
Holding a credential beyond the callback or reading another task's credentials
has different lifetime rules.

Real IDs express underlying identity, effective IDs drive many permission
checks, saved IDs support set-ID restoration, and filesystem IDs drive
filesystem checks.
The primary GID is separate from the supplementary group list, which this record
does not render.

## Separate the credential source from the numeric view

`current_cred()` describes the materializing task, not the opener.
`seq_user_ns(output)` supplies the user namespace stored in
`output->file->f_cred`, the immutable credential captured at open time.
Translate every `kuid_t` and `kgid_t` through that opener namespace with
`from_kuid_munged()` and `from_kgid_munged()`.

This matches `/proc/<pid>/status`.
If an ID has no mapping in the opener's user namespace, the `*_munged()` helper
emits the configured overflow ID.

An inherited or passed descriptor can therefore combine identity from the
materializing task with numeric UID/GID coordinates from the opener.
That split is the required record contract.

## Capabilities also have a namespace

Print the permitted set, from which capabilities may be enabled, and the
effective set used by current capability checks.
Use the same credential snapshot for both.
The in-tree proc implementation uses `seq_put_hex_ll()`, but Linux v7.2 does not
export that symbol to external modules.
Render the identical fixed-width form with `seq_printf(..., "%016llx", ...)`
and an explicit `unsigned long long` cast.

For `cap_userns`, compare `credentials->user_ns` with `init_user_ns` and print
`initial` or `nested`.
Nested UID 0 with full capabilities does not imply authority over objects in the
initial namespace; the record describes context, not a decision.

Do not print inheritable, bounding, or ambient sets; this exercise performs no
`execve()` transition.

## Your task

Edit only `interface/proc.c`.

In `rk_status_show()`:

1. take one `current_cred()` snapshot
2. obtain the opener's display namespace with `seq_user_ns()`
3. translate and print all four UID and GID roles
4. identify the credential's capability namespace
5. print the permitted and effective masks

Keep the task attribution, command-name escaping, state access, proc
publication, and teardown unchanged.
Do not prepare, mutate, retain, or replace credentials in this exercise.

Run:

```bash
./rootkitlings check
```

The checker compares the record with `/proc/thread-self/status` across root,
unprivileged, distinct-ID, changed-after-open, and nested-namespace cases.
It also proves cross-namespace descriptor semantics, rollback, clean unload, and
repeated load cycles.

## Official Linux v7.2 references

- [Credential model and lifetime][ref-1]
- [`struct cred` and `current_cred()`][ref-2]
- [UID and GID translation][ref-3]
- [`seq_user_ns()`][ref-4]
- [`/proc/<pid>/status` identity rendering][ref-5]
- [`/proc/<pid>/status` capability rendering][ref-6]
- [Namespace-aware capability checks][ref-7]

[ref-1]:
  https://docs.kernel.org/7.2/security/credentials.html

[ref-2]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/cred.h

[ref-3]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/uidgid.h

[ref-4]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/seq_file.h#L248-L255

[ref-5]:
  https://github.com/torvalds/linux/blob/v7.2/fs/proc/array.c#L152-L202

[ref-6]:
  https://github.com/torvalds/linux/blob/v7.2/fs/proc/array.c#L300-L329

[ref-7]:
  https://github.com/torvalds/linux/blob/v7.2/security/commoncap.c#L56-L132
