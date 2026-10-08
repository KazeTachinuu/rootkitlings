# 04 - Reject a module load

## Init decides whether the module exists

`insmod` calls the function registered with `module_init()`.
Returning `0` commits the load.
Returning a negative errno rejects it.

```text
insmod hello.ko
       |
       v
    rk_init()
       |
       +--- return 0 --------> module appears in lsmod
       |
       +--- return -EINVAL --> insmod fails; module is absent
```

The exit function runs only after a successful load followed by `rmmod`.
It does not run when init rejects the module.
Any resource acquired before an init failure must therefore be released by init itself.
Ling 08 builds that rollback path.

## Negative errno values

Kernel functions return negative error numbers such as:

| Value | Meaning | Typical use |
|---|---|---|
| `-EINVAL` | Invalid argument | Input or configuration is unusable |
| `-ENOMEM` | Out of memory | Allocation failed |
| `-EIO` | I/O error | An operation could not complete |

The symbolic constants come from `linux/errno.h`.
Userspace receives a failed system call and `insmod` prints the corresponding error.

## What to change

Add `linux/errno.h` to the current module.
Log one message inside init so you can prove the function ran.
Then replace its successful return with:

```c
return -EINVAL;
```

Do not call the exit function yourself.
The module loader owns that decision.

## Observe it

```bash
./rootkitlings check L04
```

The check attempts a real load and verifies four facts:

- `insmod` fails;
- init's log message exists;
- `hello` never appears as a loaded module;
- the exit message does not appear.

The failed attempt remains visible in the kernel log.

Next: [05 - Parameters](../05-parameter/README.md).
