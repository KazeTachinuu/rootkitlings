# 29 - Observe function entry with a kprobe

A kprobe runs a handler immediately before a selected kernel instruction.
It is useful for debugging and tracing a function without rebuilding the kernel.

## Observe it

The module probes `do_sys_openat2` and exposes an atomic invocation count at `/proc/rk_probe`.

```bash
before=$(cat /proc/rk_probe)
cat /etc/passwd > /dev/null
after=$(cat /proc/rk_probe)
```

`after` is larger because opening the file crossed the probed function.

## How it works

Set `struct kprobe.symbol_name` to the target and `.pre_handler` to the callback.
`register_kprobe()` resolves the name and installs the probe using the mechanism selected by the kprobe subsystem.
The pre-handler runs in an atomic tracing context and must not sleep or fault on userspace memory.

The handler can run concurrently on several CPUs, so the count is atomic.
Unregister the probe before freeing any state or module code used by its handler.

## Implement it

The configured pre-handler already increments `open_count`.

In `probe_init()`, replace the placeholder error with `register_kprobe(&kp)` and keep its return value in `err`.

In `probe_exit()`, call `unregister_kprobe(&kp)` after removing the proc entry and before the handler's module code disappears.

The supplied init rollback already unregisters the probe if proc creation fails.

## Verify

```bash
./rootkitlings start L29
$EDITOR module/hello.c
./rootkitlings check L29
```

The check causes real file opens, requires the observed count to increase twice, unloads the module, and confirms that the probe's proc interface is removed.
The unload path unregisters the probe before its handler code can disappear; successful unload without a fault is the observable safety boundary.
