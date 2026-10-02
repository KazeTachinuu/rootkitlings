# 11 - Kernel linked lists hold runtime collections

## Why this step

Ling 10 stored a single message. Real modules manage collections:
task queues, connection tables, cached entries.

The kernel uses intrusive linked lists: the list node lives inside your
struct, not in a separate container. This is the data structure behind
`task_struct`, the module list, and nearly every subsystem.

## The data structure

```
  Intrusive linked list

  list_head (entries)
  +------+     +-----------+     +-----------+
  | next |---->| node.next |---->| node.next |----> (back to head)
  | prev |<----| node.prev |<----| node.prev |<----
  +------+     +-----------+     +-----------+
               | text[64]  |     | text[64]  |
               +-----------+     +-----------+

  The node is embedded IN the struct, not pointing TO it.
  container_of() recovers the outer struct from its node address.
```

In a normal linked list, the list node contains a pointer to the data.
In an intrusive list, the data struct contains the list node. This avoids
a separate allocation for the node and improves cache locality.

## Reset the experiment

```bash
git diff -- module/hello.c
git restore module/hello.c
```

## What to build

A `/proc/rk_registry` entry that maintains a list of text entries.
Writing appends a new entry to the list. Reading returns all entries.

## The list API

From `linux/list.h`:

```c
LIST_HEAD(name);                           // declare + init empty list
list_add_tail(struct list_head *new,       // append to end
              struct list_head *head);
list_for_each_entry(pos, head, member);    // iterate (read-only)
list_for_each_entry_safe(pos, tmp,         // iterate (safe to delete)
                         head, member);
list_del(struct list_head *entry);         // unlink from list
```

Define your entry struct with an embedded `list_head`:

```c
struct rk_entry {
    char text[64];
    struct list_head node;
};
```

## Writing: allocate and append

Each write creates a new `rk_entry`:

```
  echo "first" > /proc/rk_registry

  1. kmalloc(sizeof(struct rk_entry))    <- ling 05 pattern
  2. copy_from_user into e->text         <- ling 10 pattern
  3. null-terminate, strip newline
  4. list_add_tail(&e->node, &entries)   <- new: appends to list
```

If `copy_from_user` fails after `kmalloc`, `kfree` the entry before
returning `-EFAULT` (same rollback thinking from ling 06).

## Reading: iterate and format

Walk the list, formatting each entry into a buffer:

```
  cat /proc/rk_registry

  entries: "first" -> "second" -> "third"

  Output:
  first
  second
  third
```

Use `list_for_each_entry(e, &entries, node)` to iterate. The third
argument is the field name of the `list_head` inside your struct.
Format each entry with `snprintf` into a buffer, then use
`simple_read_from_buffer` as in ling 08.

## Cleanup: the safe iterator

Freeing an entry while iterating breaks the list's `next` pointer.
`list_for_each_entry_safe` takes a second variable that holds the
next pointer before the loop body runs:

```
  list_for_each_entry_safe(e, tmp, &entries, node)
       |
       v
  tmp = e->node.next     <- saved BEFORE the body
  list_del(&e->node)     <- unlink (uses e->node)
  kfree(e)               <- free (invalidates e)
       |
       v
  e = tmp                <- safe: tmp was saved
```

Write a `free_entries()` helper. Call it from exit before
`proc_remove`.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko
cat /proc/rk_registry
printf '%s' "first" > /proc/rk_registry
printf '%s' "second" > /proc/rk_registry
printf '%s' "third" > /proc/rk_registry
cat /proc/rk_registry
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_registry` exists after loading.
- `cat /proc/rk_registry` initially returns empty (no entries).
- Writing `first`, `second`, `third` then reading shows all three.
- `/proc/rk_registry` disappears after unload.

## Verify

```bash
./rootkitlings check 11
```
> **What a defender sees**: the list is unbounded. An attacker (or a bug)
> writing in a loop exhausts kernel memory. No size limit, no cleanup on full.

See also: [12 - Char device](../12-chardev/README.md).
