# 27 - Symbols

## Why this step

Not all kernel functions are exported for modules. One version-sensitive trick:
register a kprobe on the symbol name, read `kp.addr`, unregister. This
resolves symbols that kprobes permits on the running kernel.

## The concept

```
  The kprobe lookup trick:

  1. register_kprobe("commit_creds")
       |
       v
     kernel resolves "commit_creds" -> 0xffffffff81234560
     stores address in kp.addr
       |
  2. addr = kp.addr          <-- read the resolved address
       |
  3. unregister_kprobe(&kp)  <-- remove immediately
       |
       v
     The kprobe never fires. You only used registration
     as a symbol resolver.
```

Since kernel 5.7, `kallsyms_lookup_name` is no longer exported to modules.
This workaround is not a stable module API: kprobe restrictions and kernel
configuration can reject a target.

## What you need

You already know `proc_create` from ling 10. This ling uses the kprobe
subsystem to resolve unexported kernel symbols at runtime.

New includes:

```c
#include <linux/kprobes.h>   // struct kprobe, register_kprobe, unregister_kprobe
```

## Build it

Create a module that:

1. On init, resolves `kallsyms_lookup_name` using the kprobe trick.
2. Uses the resolved pointer to look up another symbol (`commit_creds`).
3. Logs both addresses.
4. `/proc/rk_symbols` displays the resolved addresses.

The kprobe lookup function:

```c
static unsigned long lookup_name(const char *name)
{
    struct kprobe kp = { .symbol_name = name };
    unsigned long addr;

    if (register_kprobe(&kp) < 0)
        return 0;
    addr = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    return addr;
}
```

The kprobe never fires. `register_kprobe` resolves the symbol name
to an address internally. You read `kp.addr` and immediately unregister.

Why not just call `kallsyms_lookup_name` directly? Since kernel 5.7, it is no
longer exported to modules. The exercise demonstrates how offensive code works
around that boundary on the course kernel.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_symbols       # shows hex addresses like 0xffffffff...
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_symbols` contains `commit_creds=` followed by a non-zero hex address.
- The address is stable across reads.
- `/proc/rk_symbols` disappears after unload.

Do not require byte-for-byte equality with `/proc/kallsyms`. On x86, kprobe
registration may move the probe past an entry instruction such as ENDBR, so
`kp.addr` can be a few bytes after the symbol's published start address.

## Verify

```bash
./rootkitlings check 27
```

> **What a defender sees**: after immediate unregister, no persistent probe is
> left in the kprobe list. The module, its proc output, and runtime monitoring
> around probe registration remain detection opportunities.

See also: [28 - Kernel random](../28-kernel-random/README.md).
