# One-time root token

A privilege trigger must change the credentials of the process that activates it.

If a separate helper performs the write, that helper becomes root and its parent shell does not.

This module generates a random 16-character token, prints it to the kernel log, and burns it after one successful use.

## Operate it

Load the module and retrieve the current token as the operator:

```sh
insmod hello.ko
token=$(dmesg | sed -n 's/.*rk_privesc token=//p' | tail -1)
printf 'token: %s\n' "$token"
```

Give that value to the supplied client running as UID 65534:

```sh
run_as 65534 65534 \
  cred_trigger_test /proc/rk_privesc "$token"
```

```text
uid=0 euid=0
```

The client opens the proc entry, writes the token, and calls `getuid()` in the same process.

That process returns from `write()` as root.

Read the public status separately:

```sh
cat /proc/rk_privesc
```

```text
triggers=1
uid=0
```

The reported UID belongs to the process reading the status.

The active token is never returned through the world-readable proc entry.

## One successful use

```text
random 64-bit value
        |
        v
  16 hex characters -----> kernel log
        |
        v
unprivileged caller writes exact token
        |
        v
prepare credentials -> become root -> count activation
        |
        v
generate and publish the next token
```

Submitting the old token again returns `EACCES`.

The next token is already present in the kernel log for the operator.

The mutex makes comparison, credential replacement, accounting, and rotation one transaction.

## Credential transition

Kernel credentials contain real, effective, saved, and filesystem UID and GID values.

Set all eight fields to `GLOBAL_ROOT_UID` and `GLOBAL_ROOT_GID`.

Never modify the immutable object returned by `current_cred()`.

`prepare_creds()` creates a private writable copy, and `commit_creds()` publishes it for the current task.

## Files

```text
main.c        module lifecycle
trigger.c     random token, proc transaction and activation count
privilege.c   current-task credential replacement
```

## Your work

Complete `elevate_current()` in `privilege.c`.

Allocate private credentials, set every UID and GID role, and commit them.

Complete the authorized path in `trigger_write()`.

Call `elevate_current()` and propagate its error.

After success, increment `trigger_count` and call `rotate_token()` before releasing `token_lock`.

The input bounds, optional newline handling, exact token comparison, initial generation, and secret erasure on unload are provided.

## Verify

```sh
./rootkitlings check C03
```

The check extracts a runtime-generated token, rejects malformed input, elevates the same UID-65534 process, proves that the token cannot be reused, accepts the rotated token, and removes the proc entry cleanly.

The writable proc entry, kernel-log secret, credential transition, and activation count remain strong defensive evidence.
