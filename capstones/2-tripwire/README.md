# Capstone 2: Tripwire

Build a module that hooks filesystem access and logs who touches
sensitive files. A visible audit tool: the good-guy version of hooking,
before you learn to go dark.

```
  any process           VFS layer              your module
  +-----------+         +-----------+          +-------------------+
  |           | open()  |           | ftrace   |                   |
  |  cat      |-------->| do_filp   |--------->| hook_open()       |
  |  /etc/    |         | _open()   |          |   |               |
  |  shadow   |         |           |          |   v               |
  +-----------+         +-----------+          | is target path?   |
                              |                |   |yes       |no  |
                              |                |   v          v    |
                              |<---------------| pr_warn()  pass   |
                              |  call original |   "uid 1000       |
                              |                |    opened          |
                              v                |    /etc/shadow"   |
                         normal open           +-------------------+

  /proc/rk_tripwire shows the access log:
  +------+---------+-----------------------------------+
  | uid  | pid     | path                              |
  +------+---------+-----------------------------------+
  | 1000 | 4521    | /etc/shadow                       |
  | 0    | 12      | /etc/passwd                       |
  +------+---------+-----------------------------------+
```

## Prerequisites

Lings 00 through 31 (foundation + interfaces + concurrency + introspection + hooking).

## What you are building

A kernel-level file access monitor. Hook `do_filp_open` with ftrace,
check if the path matches a watchlist, log the access with uid/pid/path.

- An ftrace hook intercepts `do_filp_open`.
- A static watchlist of paths to monitor (e.g., `/etc/shadow`, `/etc/passwd`).
- Each hit is stored in a list entry (uid, pid, path, timestamp).
- `/proc/rk_tripwire` dumps the access log as a formatted table.
- A sysfs parameter controls whether logging is active.

## What "done" looks like

```sh
insmod hello.ko
cat /etc/shadow
cat /proc/rk_tripwire
# [tripwire] uid=1000 pid=4521 /etc/shadow
echo 0 > /sys/module/hello/parameters/armed
cat /etc/shadow           # no new log entry
rmmod hello
```

## What a defender sees

Everything. This is the good-guy version, fully visible on purpose.

```
lsmod                                        hello is listed
cat /sys/kernel/tracing/enabled_functions     do_filp_open is hooked
cat /proc/rk_tripwire                              access log is readable
dmesg                                        load message present
```

The hook shows in tracefs because ftrace is the kernel's own tracing
framework. It was designed to be inspectable. In capstone 3, you will
learn to hide. Here, visibility is the feature.

## Contract

The check script verifies:

- `/proc/rk_tripwire` exists and is empty on load.
- Accessing `/etc/shadow` adds a `[tripwire]` entry with the path.
- Accessing `/etc/passwd` adds an entry for that path.
- Log entries contain `uid=` and `pid=`.
- Setting `armed=0` stops logging; `armed=1` resumes it.
- `/proc/rk_tripwire` disappears after unload.

## Verify

```bash
./rootkitlings check capstone-2
```

## Hints

