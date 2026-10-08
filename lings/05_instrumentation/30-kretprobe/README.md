# 30 - Observe function results with a kretprobe

A kretprobe runs a handler when a probed function returns.
Use it when the return value matters, such as separating successful I/O from errors.

## Observe it

The module probes `vfs_read` and publishes successful and failed return counts.

```text
vfs_read() ---> nonnegative result ---> success++
           +--> negative errno ------> failed++
```

```bash
cat /proc/rk_retprobe
cat /etc/passwd > /dev/null
cat /proc > /dev/null 2>&1 || true
cat /proc/rk_retprobe
```

## How it works

Set `struct kretprobe.handler` and its embedded `kp.symbol_name`.
`regs_return_value(regs)` extracts the architecture-correct return value.
A negative `long` is an encoded error and a nonnegative value is success.

Kretprobes keep per-invocation state until matching returns occur.
The kernel chooses a default active-instance limit when `maxactive` is zero and records overflow in `nmissed`.
The handler runs concurrently and therefore updates atomic counters.

## Implement it

In the handler, read the result and increment exactly one counter.

## Verify

```bash
./rootkitlings start L30
$EDITOR .rootkitlings/work/kretprobe/hello.c
./rootkitlings check L30
```

The check performs one successful read and one read that returns an error, then observes both counters.
