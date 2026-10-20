# M00: Persist Across Reboot and Hide from `lsmod`

## Mission

L09 gave the module a command table, but it disappears on reboot and anyone can
find it with `lsmod`.
Two problems, two mechanisms: write a boot configuration file so `systemd`
reloads the module automatically, and remove the module's own list entry so
`lsmod` and `/proc/modules` no longer show it.

## What you will observe

After building and loading the module, persistence writes a configuration file
that survives reboot, and hiding removes the module from published lists without
stopping its code:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# mkdir rklings.persist
root@victim:~# cat /etc/modules-load.d/rklings.conf
rklings
root@victim:~# mkdir rklings.h.m
root@victim:~# lsmod | grep rklings
root@victim:~# grep '^rklings ' /proc/modules
root@victim:~# mkdir rklings.ping
mkdir: File exists
```

The empty `lsmod` and `grep` output proves the module is hidden.
The `mkdir rklings.ping` response proves it is still running.

## Workspace

```text
exercises/M00-persistence-stealth
├── Kbuild
├── core
│   ├── cred.c          (supplied: PID escalation)
│   ├── dispatch.c      (supplied: command registry and kretprobes)
│   ├── init.c          (supplied: lifecycle, ping, status)
│   └── module.c        (editable)
└── include
    └── rootkit.h       (supplied: types, macros, declarations)
```

Only `core/module.c` is editable.
The dispatcher, escalation, and lifecycle code are supplied from L09.

## Persistence via `modules-load.d`

`systemd-modules-load` reads every `.conf` file in `/etc/modules-load.d/` at
boot and asks `modprobe` to load each named module.
The module must also be installed under `/lib/modules/$(uname -r)/` and indexed
by `depmod` so `modprobe` can find it.
The validator handles the install and `depmod` steps; the exercise command owns
only the configuration file.

Three kernel functions perform file I/O from module context:

- `filp_open(path, flags, mode)` opens a file and returns a `struct file *`,
  or an error pointer on failure (check with `IS_ERR`, extract with `PTR_ERR`).
- `kernel_write(file, buf, count, &pos)` writes `count` bytes from `buf`.
- `filp_close(file, NULL)` releases the file.

`O_WRONLY | O_CREAT | O_TRUNC` opens for writing, creates if absent, and
replaces existing content.

The persist command writes `MODULE_NAME_STR "\n"` (defined in `rootkit.h`)
with `sizeof(MODULE_NAME_STR)` bytes.
`sizeof("rklings")` is 8: 7 characters plus the NUL terminator.
That matches the 8 bytes in `"rklings\n"` (7 characters plus newline), so the
newline is written and the NUL is not.

The unpersist command opens the same path with `O_TRUNC` and writes nothing,
leaving an empty file.

## Module-list hiding

The kernel tracks loaded modules in a doubly-linked list anchored at
`THIS_MODULE->list`.
`lsmod` and `/proc/modules` iterate this list, so removing the node hides the
module from those inventories.

`list_del(&THIS_MODULE->list)` removes the node.
`list_add(&THIS_MODULE->list, saved_prev)` inserts it back after `saved_prev`.
Save the previous pointer before removing so the node can be restored later.

Both operations must be idempotent.
`saved_prev` doubles as a state marker: non-NULL means hidden.
`rk_module_hide` returns immediately when `saved_prev` is already set.
`rk_module_unhide` returns immediately when `saved_prev` is NULL, and clears it
after restoring.

The module keeps running; it just disappears from the published lists.
`init.c` calls `rk_module_unhide()` during exit so `rmmod` can find the module.

## TODOs

```text
TODO 1  core/module.c:14:2  hide the module from the published list
TODO 2  core/module.c:19:2  restore the module in the published list
TODO 3  core/module.c:26:2  write the module name to PERSIST_PATH
TODO 4  core/module.c:32:2  clear PERSIST_PATH
```

### TODO 1 -- `core/module.c:14:2` -- Hide the module from the published list

Guard against repeated calls: if `saved_prev` is already set, return
immediately.
Otherwise save `THIS_MODULE->list.prev` and call `list_del` on
`THIS_MODULE->list`.

### TODO 2 -- `core/module.c:19:2` -- Restore the module in the published list

Guard against repeated calls: if `saved_prev` is NULL, return immediately.
Otherwise call `list_add` to insert `THIS_MODULE->list` after `saved_prev`, then
set `saved_prev` to NULL.

### TODO 3 -- `core/module.c:26:2` -- Write the module name to `PERSIST_PATH`

1. Declare a `struct file *` and a `loff_t pos = 0`.
2. Open `PERSIST_PATH` with `O_WRONLY | O_CREAT | O_TRUNC` and mode `0644`.
3. If the open fails, return the error with `PTR_ERR`.
4. Write `MODULE_NAME_STR "\n"` with length `sizeof(MODULE_NAME_STR)`.
5. Close the file and return `0`.

### TODO 4 -- `core/module.c:32:2` -- Clear `PERSIST_PATH`

Same open pattern as TODO 3.
Write zero bytes (empty string with count 0), close, and return `0`.

## Test it

From the host:

```console
$ ./rklings run M00
```

Inside the guest:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# install -D -m 0644 /rklings/rklings.ko \
  /lib/modules/$(uname -r)/extra/rklings.ko
root@victim:~# depmod -a
root@victim:~# mkdir rklings.persist
root@victim:~# cat /etc/modules-load.d/rklings.conf
rklings
root@victim:~# mkdir rklings.h.m
root@victim:~# lsmod | grep rklings
root@victim:~# mkdir rklings.u.m
root@victim:~# lsmod | grep rklings
rklings  16384  0
root@victim:~# mkdir rklings.unpersist
root@victim:~# cat /etc/modules-load.d/rklings.conf
```

## Validate

From the repository root:

```console
$ ./rklings validate M00
```

Validation proves: persist writes the configuration file; the module loads
automatically after a real reboot; unpersist clears the file; the module stays
absent after a second reboot; hide removes the module from `lsmod` while it
keeps running; unhide restores it; repeated hide and unhide do not corrupt the
list; escalation and dispatcher commands still work.

## Next

The module survives reboot and disappears from `lsmod`, but the files,
processes, text, and ports associated with it remain visible.
L10 builds a shared target list that later lessons will use to hide specific
items from directory listings.
