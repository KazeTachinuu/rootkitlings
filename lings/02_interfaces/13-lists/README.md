# 13 - Store records in a kernel list

An intrusive linked list stores its link inside each record.

This module appends up to 32 text records through `/proc/rk_registry` and returns them in insertion order.

## Observe it

```sh
insmod hello.ko
printf first > /proc/rk_registry
printf second > /proc/rk_registry
printf third > /proc/rk_registry
cat /proc/rk_registry
```

```text
first
second
third
```

The thirty-third accepted-size record fails with `ENOSPC` and leaves the first 32 records intact.

## Intrusive ownership

```text
entries head
    |
    v
+---------+    +---------+    +---------+
| first   |--->| second  |--->| third   |
| node    |<---| node    |<---| node    |
+---------+    +---------+    +---------+
```

`LIST_HEAD(entries)` creates the empty sentinel.

`list_add_tail()` preserves write order.

`list_for_each_entry()` recovers each outer `registry_entry` from its embedded `node`.

The proc reader and writers can run concurrently, so every traversal and mutation uses `entries_lock`.

The explicit `ENTRY_MAX` bound prevents an untrusted writer from consuming unlimited kernel memory.

## Add and remove records

The write path rejects empty, oversized, and embedded-null input before publication.

It allocates and fills a candidate before taking the mutex.

Under the mutex, it checks `entry_count`, appends the node, and increments the count as one state transition.

Cleanup requires `list_for_each_entry_safe()` because freeing the current record invalidates its node.

Module exit removes `/proc/rk_registry` before purging the list so no new callback can begin while records are being freed.

## Implement it

In the workspace `hello.c`, declare one record pointer in `registry_show()` and emit each `text` field with `seq_printf()` while holding `entries_lock`.

In `registry_write()`, append the prepared record with `list_add_tail()` at the marked line.

In `purge_entries()`, declare current and next pointers, iterate with `list_for_each_entry_safe()`, unlink each node, and free each record.

Keep `entry_count = 0` under the mutex after the purge.

## Verify

```sh
./rootkitlings check L13
```

The verification reads the initially empty list, checks exact insertion order, fills all 32 slots, proves the next write fails without corruption, and confirms proc cleanup.

The list is bounded and synchronized, but its contents and control path remain visible to any process allowed by mode `0600`.

Next: [14 - Register a character device](../14-chardev/README.md).
