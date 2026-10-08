# 34 - Hide one module-list entry

`lsmod` and `/proc/modules` enumerate the kernel's module list.

Removing one list node makes those two views omit the module without unloading its code.

Other kernel structures still retain it.

## Compare the views

Load the completed module:

```sh
insmod hello.ko
lsmod | grep hello
test -d /sys/module/hello && echo 'sysfs sees hello'
cat /proc/rk_cloak
```

```text
sysfs sees hello
hidden=yes
```

The module disappeared from the list used by `lsmod`, but its sysfs object and direct proc control path remain.

```text
                hello.ko is loaded
                        |
             +----------+----------+
             |                     |
             v                     v
       module list              sysfs object
             |                     |
       remove one node          unchanged
             |                     |
             v                     v
       lsmod omits hello     /sys/module/hello exists
```

This disagreement is the detection technique.

## Reveal before removal

`rmmod hello` searches the module list by name.

Restore the saved list position before asking it to unload:

```sh
echo reveal > /proc/rk_cloak
cat /proc/rk_cloak
lsmod | grep hello
rmmod hello
```

```text
hidden=no
hello ...
```

The control entry is deliberately reachable by exact path while the module is hidden.

## Your work

```sh
./rootkitlings start L34
$EDITOR .rootkitlings/work/module-list-hiding/hello.c
```

Complete `hide_module()`.

Save `THIS_MODULE->list.prev`, call `list_del_init()` on the module node, and set `hidden` to true.

Complete `reveal_module()`.

Call `list_add()` with the saved predecessor and set `hidden` to false.

Both functions must remain idempotent.

The proc parser and exit-time safety call are provided.

## Verify

```sh
./rootkitlings check L34
```

The check proves the `lsmod`/sysfs disagreement, rejects an unknown command, reveals the module, unloads it normally, and confirms that procfs and sysfs are clean.

The saved neighbor is stable only in this controlled VM without concurrent module churn.

Direct module-list manipulation is unsupported kernel internals, not a production extension API.
