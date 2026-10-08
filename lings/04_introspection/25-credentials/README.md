# 25 - Read the caller's credentials

Every task has credentials describing its user and group identities.
Kernel authorization and auditing code inspect these credentials at the point of an operation.

## Observe it

The same proc callback reports different values for root and an unprivileged reader.

```bash
cat /proc/rk_creds
# uid=0 gid=0

chpst -u 65534:65534 cat /proc/rk_creds
# uid=65534 gid=65534
```

## How it works

`current_cred()` returns a read-only credential snapshot for the current task.
The credential stores `kuid_t` and `kgid_t`, not plain integers.
`from_kuid(&init_user_ns, cred->uid)` and `from_kgid()` convert them for display in the initial user namespace.

Do not access representation fields such as `.val` directly.
Namespace-aware helpers preserve the distinction between kernel IDs and userspace numbers.

## Implement it

Read `current_cred()` in `info_show()`.
Print its UID and GID after converting them through `init_user_ns`.

## Verify

```bash
./rootkitlings start L25
$EDITOR module/hello.c
./rootkitlings check L25
```

The check performs real reads as UID 0 and UID 65534.
