# 46 - Raw socket

## What we are learning

Kernel modules can create sockets directly with `sock_create_kern`,
bypassing userspace entirely. Unlike a netfilter hook (which only
receives packets), a raw socket lets the module send crafted packets:
reverse shells, exfiltration, or covert communication.

The socket belongs to the kernel, not a userspace process. It has no useful PID
owner, but the socket itself remains visible in kernel network tables such as
`/proc/net/raw`.

## The concept

```
  Userspace socket:                 Kernel socket:

  app calls socket()               module calls sock_create_kern()
       |                                |
       v                                v
  process owner:                   process owner:
    userspace PID                    none

  socket table:
    /proc/net/raw                    /proc/net/raw

  Both go through:
    IP layer -> network driver -> wire
```

A kernel-owned socket has no associated userspace process. Process attribution
is therefore weaker, but socket-table and packet-level visibility remain.

## What we are doing

Create a raw ICMP socket in kernel space at module load, expose its
status through `/proc/rk_rawsock`, and release it cleanly on unload.

## Includes you need

```c
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <net/net_namespace.h>
```

`net_namespace.h` provides `init_net` (the default network namespace).
`linux/in.h` provides `IPPROTO_ICMP`.

## Key code

```c
static struct socket *raw_sock;

// In init:
err = sock_create_kern(&init_net, AF_INET, SOCK_RAW, IPPROTO_ICMP,
                       &raw_sock);

// In exit:
if (raw_sock)
    sock_release(raw_sock);
```

`sock_create_kern` takes the network namespace, address family, socket
type, protocol, and a pointer to store the result. The `_kern` variant
marks the socket as kernel-owned.

## Output format

```
socket=open
type=RAW
proto=ICMP
```

## Try it

```bash
./rootkitlings run --network
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_rawsock                 # socket=open
dmesg | tail
rmmod hello
dmesg | tail                    # socket released
```

## Contract

The check script verifies:

- `/proc/rk_rawsock` contains `socket=open`, `type=RAW`, and `proto=ICMP`.
- A raw socket is visible in `/proc/net/raw`.
- `/proc/rk_rawsock` disappears after unload.

## Verify

```bash
./rootkitlings check 46
```

> **What a defender sees**: `dmesg` shows the socket creation message.
> `/proc/net/raw` exposes the socket even though no userspace PID owns it.
> Packet capture exposes the traffic independently of socket attribution.

See also: [47 - DNS spoof](../47-dns-spoof/README.md).
