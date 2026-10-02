# Capstone 0: Message board

Build a module that serves a configurable message through `/proc/rk_board`.

```
             sysfs                          procfs
  printf 'phantom' > /sys/.../name     cat /proc/rk_board
             |                              |
             v                              v
       param callback              board_read callback
             |                              |
             v                              v
       +----------------------------------+
       |  struct board_state               |
       |    char  message[64]              |
       |    unsigned long reads            |
       +----------------------------------+
             |                              |
             v                              v
       stores message               returns: "hello phantom (3 reads)\n"
```

## Prerequisites

Lings 00 through 08 (all of foundation).

## What you are building

A single module that combines every foundation primitive:

- A `struct board_state` on the heap holds a message and a read counter.
- A sysfs parameter `name` with a callback updates the message.
- `/proc/rk_board` reads from the struct and increments the counter.
- Init rolls back on partial failure (kmalloc succeeds, proc_create fails).
- Exit releases everything in reverse order.

## What "done" looks like

```sh
insmod hello.ko
cat /proc/rk_board              # "hello world (1 reads)"
cat /proc/rk_board              # "hello world (2 reads)"
printf '%s' phantom > /sys/module/hello/parameters/name
cat /proc/rk_board              # "hello phantom (3 reads)"
rmmod hello
```

Each `cat` increments the counter. Changing the name changes the greeting.
The counter proves the heap struct persists across reads.

## What a defender sees

The module is visible in `lsmod`, the parameter is world-readable in sysfs,
`/proc/rk_board` is visible to every user, and `dmesg` records every load.
Everything is exposed. Hiding comes later.

## Contract

The check script verifies:

- `/proc/rk_board` output format: `hello NAME (N reads)\n` where N increments per read.
- The sysfs parameter callback fires on each name change.
- Default name is `world` (first read returns `hello world (1 reads)`).

## Verify

```bash
./rootkitlings check capstone-0
```

## Hints

Only if stuck. Try without them first.

