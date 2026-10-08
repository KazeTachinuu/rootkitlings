# Live configuration store

A long-running kernel component needs settings that can change without unloading it.

This module keeps named values in kernel memory and exposes them through `/proc/rk_cfg`.

Each write performs one operation:

```text
set:<key>=<value>    create or replace a value
del:<key>            delete a value
```

Reading the entry lists the current values in insertion order.

## Operate it

Load the module and add two settings:

```sh
insmod hello.ko
echo 'set:target=192.168.1.10' > /proc/rk_cfg
echo 'set:interval=60' > /proc/rk_cfg
cat /proc/rk_cfg
```

```text
target=192.168.1.10
interval=60
```

Replace one value and delete the other:

```sh
echo 'set:interval=30' > /proc/rk_cfg
echo 'del:target' > /proc/rk_cfg
cat /proc/rk_cfg
```

```text
interval=30
```

Setting an existing key updates its list item instead of creating a duplicate.

Malformed, empty, and oversized commands fail without changing the store.

## Command and ownership flow

```text
write /proc/rk_cfg
         |
         v
  copy and parse command
         |
         v
  lock -> find / add / delete -> unlock
         |
         v
read /proc/rk_cfg as key=value lines
```

`proc.c` owns the userspace boundary.

It bounds and copies one command before parsing it.

`store.c` owns the list and its mutex.

Every new item is allocated when `set:` succeeds and freed by `del:` or module removal.

`config_store_find()` returns a pointer into the list, so callers use it only while holding `config_lock`.

## Files

```text
main.c      proc-entry lifecycle and final store cleanup
proc.c      bounded command copy, parsing and dispatch
store.c     mutex, list items and store operations
config.h    shared sizes and component contracts
```

Kbuild links those translation units into one `hello.ko`.

## Your work

Complete `config_write()` in `proc.c`.

Reject empty input and any command that cannot fit in `CONFIG_COMMAND_SIZE`.

Copy exactly `count` bytes, append a null byte, trim surrounding whitespace, and dispatch `set:` or `del:`.

For `set:`, split at the first `=` and reject a missing separator.

Return the original byte count only when the operation succeeds.

Complete the store operations in `store.c`.

`config_store_set()` must reject empty or oversized fields, replace an existing value under the mutex, or allocate and append one new item.

`config_store_delete()` must unlink and free the matching item when it exists.

`config_store_show()` must emit one `key=value` line per item while the list is protected.

`config_store_clear()` must safely unlink and free every remaining item during unload.

Every error path must release the mutex and preserve existing entries.

## Verify

```sh
./rootkitlings check C00
```

The verification performs real insert, replacement, deletion, malformed-input, oversized-input, concurrent-access, and unload operations against the module.

The proc entry, loaded module, and stored settings remain visible to ordinary system inspection.
