# 31 - Trace several functions with one fprobe

Fprobe attaches callbacks through the ftrace function-entry infrastructure.
It is designed for tracing one or many functions with less per-target setup than individual kprobes.

## Observe it

The module attaches one probe to `do_sys_openat2` and `vfs_read`, then publishes a separate count for each target at `/proc/rk_fprobe`.

```bash
cat /proc/rk_fprobe
printf note > /tmp/note
cat /tmp/note > /dev/null
cat /proc/rk_fprobe
```

```text
opens=2
reads=2
missed=0
```

## How it works

Assign the `entry_handler` in a `struct fprobe`.
`register_fprobe_syms()` attaches that probe to an array of exact symbol names in one operation.

The entry handler receives instruction addresses and register state.
It runs in tracing context and must not sleep.
`sprint_symbol_no_offset()` turns the entry address into the exact target name so the shared handler can update the matching counter.
`nmissed` reports entries that could not be handled, which is useful when judging trace completeness.

Always call `unregister_fprobe()` before module code or handler state disappears.

## Implement it

The supplied `count_io()` handler already attributes each callback to `open_count` or `read_count`, and `io_probe.entry_handler` already points to it.

In init, register `io_probe` for every entry in `io_symbols` with:

```c
register_fprobe_syms(&io_probe, io_symbols, ARRAY_SIZE(io_symbols))
```

Keep the returned status in `err`.

In exit, call `unregister_fprobe(&io_probe)` after removing the proc entry and before the handler disappears.

The supplied init rollback already unregisters the probe if proc creation fails.

## Verify

```bash
./rootkitlings start L31
$EDITOR module/hello.c
./rootkitlings check L31
```

The check performs real opens and reads, requires both target counters to increase independently, and confirms clean removal.
