# 42 - Port hiding

## Why this step

`netstat` and `ss` read `/proc/net/tcp` to list open ports. A rootkit
hooks `tcp4_seq_show` (the function that renders each line) and skips
entries whose source port matches the hidden port.

This technique is used by Diamorphine, Reptile, and most LKM rootkits.

## What you already know

From ling 38, you hooked getdents64. From ling 39, you filtered
content. This ling hooks the specific function that renders
`/proc/net/tcp` entries.

## The concept

```
  cat /proc/net/tcp
       |
       v
  seq_file iterator calls tcp4_seq_show() once per line:

  tcp4_seq_show(seq, SEQ_START_TOKEN)  -->  "  sl  local_address ..."
  tcp4_seq_show(seq, sock_A)           -->  "  0: 0100007F:0050 ..."
  tcp4_seq_show(seq, sock_B)           -->  (port 8080? skip!)
  tcp4_seq_show(seq, sock_C)           -->  "  2: 00000000:0016 ..."

  Your hook:
  +---> v == SEQ_START_TOKEN?  pass through (header row)
  +---> inet_sk(v)->inet_sport == hidden?  redirect to skip_entry()
  +---> otherwise?  let original render the line
```

Unlike ling 38 (post-process a buffer), here you intercept BEFORE
each line renders. Simpler: no buffer copying, no memmove.

## Build it

You need:

```c
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <net/inet_sock.h>
#include <net/tcp.h>
```

Resolve `tcp4_seq_show` via kprobe, hook it with ftrace (same flags
as ling 38). In the callback, check `inet_sk(v)->inet_sport`:

```c
if (inet_sk(v)->inet_sport == htons(HIDDEN_PORT))
    regs->ip = (unsigned long)skip_entry;  // redirect to no-op
```

`skip_entry` just returns 0 (renders nothing). Also create
`/proc/rk_portcloak` with port number and hidden count.

## Hints

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
nc -l -p 8080 &
cat /proc/net/tcp               # port 8080 (0x1F90) is visible
insmod hello.ko
cat /proc/net/tcp               # port 8080 no longer listed
cat /proc/rk_portcloak                 # shows hidden port
kill %1
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_portcloak` contains `port=8080` and `hidden=N`.
- Port 8080 disappears from `/proc/net/tcp` while the module is loaded.
- `hidden` counter increments on each filtered entry.
- Port reappears in `/proc/net/tcp` after unload.

## Verify

```bash
./rootkitlings check 42
```

The check starts a listener on port 8080, verifies it appears in
`/proc/net/tcp`, loads the module, and verifies it disappears.

> **What a defender sees**: `nmap` scans find open ports that `ss` does
> not list. Comparing `nmap` output with `ss` output reveals the hidden
> port.

See also: [Networking](../../06_networking/README.md).
