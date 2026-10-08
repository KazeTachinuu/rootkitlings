# 35 - Sysfs artifact lifecycle

Loading a module creates more evidence than one row in `lsmod`.

Subsystems publish their own objects in sysfs.
Those objects remain independently observable until their owner removes them.

```text
module state                 observer

marker created  --------->  /sys/kernel/rk_marker/status
marker removed  --------->  path disappears
```

Here the module owns a harmless marker beneath `/sys/kernel`.
The marker has one read-only attribute:

```console
# cat /sys/kernel/rk_marker/status
visible
```

`/proc/rk_sysfs` controls that marker without touching the module's own kobject:

```console
# cat /proc/rk_sysfs
visible=yes
# echo hide > /proc/rk_sysfs
# test ! -e /sys/kernel/rk_marker/status && echo hidden
hidden
# echo show > /proc/rk_sysfs
# cat /sys/kernel/rk_marker/status
visible
```

## Build it

```sh
./rootkitlings start L35
$EDITOR module/hello.c
```

Study how `marker_create_locked()` allocates `rk_marker`, publishes `status`, and rolls back if publishing fails.

Complete `marker_destroy_locked()` in exact reverse order: remove the attribute, drop the kobject reference, and clear the pointer.

Both helpers run with `marker_lock` held.

```text
create                          destroy

kobject_create_and_add         sysfs_remove_file
          |                             |
          v                             v
sysfs_create_file             kobject_put
          |                             |
          v                             v
     published                    pointer = NULL
```

Then verify the real lifecycle:

```sh
./rootkitlings check L35
```

An unknown command must change nothing.
Repeating `hide` or `show` must fail instead of silently masking a state error.

Unloading must remove both `/proc/rk_sysfs` and `/sys/kernel/rk_marker`.
