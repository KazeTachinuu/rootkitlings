# 26 - Replace a task's credentials

Linux changes a task's identity by preparing a new credential object and committing it atomically.
The same mechanism supports legitimate transitions such as login, set-ID execution, and namespace operations.

## Observe it

An unprivileged shell writes an exact command to `/proc/rk_escalate` and then reads its own new identity.

```text
uid=65534 task ---> prepare copy ---> set all identity roles ---> commit
                                                               |
                                                               v
                                                            uid=0 task
```

`commit_creds()` affects the task executing the proc write callback.
It does not change the module loader or an unrelated shell.

## How it works

`prepare_creds()` allocates a mutable copy of the caller's credentials.
Return `-ENOMEM` if it fails.
Set real, effective, saved, and filesystem UID/GID fields with `GLOBAL_ROOT_UID` and `GLOBAL_ROOT_GID`.
`commit_creds()` publishes the new object and consumes the prepared reference.

The write handler accepts only `escalate`, with an optional trailing newline.
Rejecting every other command avoids accidental activation from a shared prefix.

## Implement it

Complete `escalate_to_root()` with `prepare_creds()`, all eight identity assignments, and `commit_creds()`.

## Verify

```bash
./rootkitlings start L26
$EDITOR .rootkitlings/work/credential-replacement/hello.c
./rootkitlings check L26
```

The check proves that an unrelated command has no effect and that the exact command changes the writing process from UID 65534 to UID 0.
