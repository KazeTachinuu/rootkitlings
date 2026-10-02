# 42 - Port hiding

## Why this step

`netstat` and `ss` read `/proc/net/tcp` to list open ports. A rootkit
hooks `tcp4_seq_show` (the function that renders each line) and hides
entries whose source port matches the hidden port.

This technique is used by Diamorphine, Reptile, and most LKM rootkits.

## What you already know

From ling 38, you hooked getdents64 with a kretprobe. From ling 39,
you filtered content. This ling hooks the specific function that
renders `/proc/net/tcp` entries.

## The concept

`/proc/net/tcp` is a seq_file. The kernel calls `tcp4_seq_show(seq, v)`
once per entry. Each call appends one line to `seq->buf` by advancing
`seq->count`. Your kretprobe uses the "count rewind" trick: save
`seq->count` before the call, and if the entry should be hidden,
restore the saved count after, effectively "unwriting" that line.

```
  cat /proc/net/tcp
       |
       v
  seq_file iterator calls tcp4_seq_show() once per line:

  tcp4_seq_show(seq, sock_A)
       entry_handler:
       +---> save seq->count
       +---> check inet_sk(sock_A)->inet_sport
       +---> port matches? set hide = true
       |
       original tcp4_seq_show runs, appends line to seq->buf
       |
       ret_handler:
       +---> if hide: seq->count = saved_count (line erased)
       +---> otherwise: line stays in output
```

Unlike lings 38-40 (post-process a userspace buffer), here you
intercept a kernel-internal seq_file writer. No `copy_from_user` or
`copy_to_user` needed.

**Important**: `tcp4_seq_show` is a normal kernel function, not a
syscall wrapper. So `regs->di` IS the first argument (`seq_file*`)
directly, not a pointer to another `pt_regs`. No double indirection.

## Build it

You need:

```c
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <net/inet_sock.h>
#include <net/tcp.h>
```

Your context struct stores three things:

```c
struct tcp_ctx {
    struct seq_file *seq;
    size_t saved_count;
    bool hide;
};
```

Register a kretprobe on `tcp4_seq_show`. The entry handler:

1. Gets `seq` from `regs->di` and `v` from `regs->si` (direct args,
   no double indirection).
2. Saves `seq->count` (current write position in seq buffer).
3. If `v == SEQ_START_TOKEN`, returns 0 (header row, pass through).
4. Otherwise, checks `inet_sk((struct sock *)v)->inet_sport` against
   `htons(HIDDEN_PORT)`. Sets `hide = true` if it matches.

The return handler: if `hide` is true, restores `seq->count` to the
saved value (erasing the line just written) and increments a counter.

Also create `/proc/rk_portcloak` with `port=8080` and `hidden=N`.

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
