# Capstone 0: Config store

Build a kernel-space configuration store with command execution.
Every rootkit stores its config in kernel memory, invisible to `find`
and `grep`. This is yours.

```
  userspace                              kernel module
  +-------------------+                  +-----------------------------+
  |                   |  write to proc   |                             |
  | echo "set:target  |----------------->|  proc_write()               |
  |  =192.168.1.1"    |                  |    |                        |
  |   > /proc/rk_cfg  |                  |    v                        |
  +-------------------+                  |  +-----------------------+  |
                                         |  | struct config_entry   |  |
  +-------------------+                  |  |   char key[32]        |  |
  |                   |  read proc       |  |   char value[64]      |  |
  | cat /proc/rk_cfg  |<----------------|  +-----------------------+  |
  |                   |                  |    |           |            |
  +-------------------+                  |    v           v            |
                                         |  linked       linked       |
  +-------------------+                  |  list          list         |
  |                   |  ioctl           |                             |
  | rk_ctl --clear    |----------------->|  CMD_CLEAR / CMD_EXEC      |
  |                   |                  |                             |
  +-------------------+                  +-----------------------------+
```

## Prerequisites

Lings 00 through 16 (foundation + interfaces).

## What you are building

A rootkit's internal brain. Two interfaces to the same data:

- `/proc/rk_cfg` accepts writes: `set:key=value`, `del:key`, `list`, `exec:CMD`.
- `/dev/rk_cfg` accepts ioctl: `CMD_SET`, `CMD_DEL`, `CMD_LIST`, `CMD_EXEC`, `CMD_CLEAR`.
- Config entries live in a linked list on the heap.
- `exec:COMMAND` runs a userspace command via `call_usermodehelper`,
  captures stdout to a temp file, reads it back with `filp_open`/`kernel_read`,
  and stores the output as a config entry with key `_result`.
- `cat /proc/rk_cfg` dumps all key=value pairs.
- Exit frees the list, removes proc and device entries.

## What "done" looks like

```sh
insmod hello.ko
echo "set:target=192.168.1.1" > /proc/rk_cfg
echo "set:interval=60" > /proc/rk_cfg
cat /proc/rk_cfg
# target=192.168.1.1
# interval=60
echo "exec:uname -r" > /proc/rk_cfg
cat /proc/rk_cfg
# target=192.168.1.1
# interval=60
# _result=6.12.0
echo "del:target" > /proc/rk_cfg
cat /proc/rk_cfg
# interval=60
# _result=6.12.0
rmmod hello
```

## Structure

This outgrows a single file. Split it:

```
  capstones/0-config-store/
  +-- Kbuild          hello-y := main.o store.o exec.o
  +-- main.c          init/exit, proc setup, miscdevice
  +-- store.c         linked list, set/del/list logic
  +-- store.h         struct config_entry, function prototypes
  +-- exec.c          call_usermodehelper wrapper, output capture
  +-- exec.h          int rk_exec(const char *cmd, char *out, size_t len)
```

## What a defender sees

`/dev/rk_cfg` appears in the device list. `/proc/rk_cfg` is world-readable.
`strace` shows the ioctl numbers in plain sight. `ps` shows the executed
command briefly. No hiding yet.

## Contract

The check script verifies:

- `/proc/rk_cfg` and `/dev/rk_cfg` exist after loading.
- Store is empty on start.
- `set:key=value` stores an entry (visible in `cat`).
- `del:key` removes it.
- `exec:id` runs the command and `_result` contains `uid=0`.
- `exec:uname -r` output matches the running kernel.
- Multiple entries coexist (set three, read three).
- Entries disappear after unload.

## Verify

```bash
./rootkitlings check capstone-0
```

## Hints

Only if stuck. Try without them first.
