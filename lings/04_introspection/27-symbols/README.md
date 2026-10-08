# 27 - Resolve a kernel symbol with a temporary kprobe

Kernel modules may call only exported symbols directly.
A named kprobe asks the tracing subsystem to locate a probeable function even when its address is not exported.

## Observe it

The module resolves `commit_creds` once and publishes the nonzero address through `/proc/rk_symbols`.

```bash
cat /proc/rk_symbols
# commit_creds=0xffffffff........
```

The address is stable while the kernel is running but changes across boots when address randomization is enabled.

## How it works

```text
symbol name ---> register_kprobe() ---> kp.addr ---> unregister_kprobe()
```

Initialize a local `struct kprobe` with `.symbol_name = name`.
After successful registration, copy `kp.addr` before unregistering the probe.
The temporary probe is used only for lookup and must not remain installed.

Registration can fail for an unknown, blacklisted, or policy-restricted target.
Runtime symbol lookup is version-sensitive and should not replace stable exported interfaces in production modules.

## Implement it

Complete `resolve_symbol()` so every successful registration has a matching unregister and failures return zero.

## Verify

```bash
./rootkitlings start L27
$EDITOR module/hello.c
./rootkitlings check L27
```

The check reads the actual resolved address twice and requires a stable nonzero value.
