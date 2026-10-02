# 51 - Packet mangling

## What we are learning

Rootkits redirect traffic in flight to intercept connections, reroute
C2 channels, or perform MITM attacks. This ling demonstrates UDP port
redirection: packets aimed at one port silently arrive at another.
This is the core of service impersonation and traffic hijacking.

## What we are doing

Hook `NF_INET_LOCAL_OUT` to intercept outgoing UDP packets. Rewrite
the destination port from 7777 to 9999:

```
  app sends to :7777         listener on :9999
       |                          ^
       v                          |
  +-----------------------------+ |
  | LOCAL_OUT hook              | |
  |   dest 7777 --> dest 9999  |-+
  +-----------------------------+

  The sender thinks it sent to :7777.
  The listener receives on :9999.
  Nobody in between sees the swap.
```

## New API

One new header for UDP:

```c
#include <linux/udp.h>
```

`struct udphdr` has `dest` and `check` fields, both in network byte
order. Compare ports with `htons()`:

```c
#define TRAP_PORT  htons(7777)
```

**Writable check**: skbs can be cloned (shared data). You must call
`skb_ensure_writable` before modifying any header:

```
  skb (cloned)
    |
    +---> skb->data ----+
                        |     shared buffer
  skb2 (clone) ---------+---> [ IP | UDP | payload ]
                               writing here corrupts skb2!

  skb_ensure_writable(skb, len)
    |
    +---> copies the shared region into a private buffer
    +---> skb->data may now point somewhere new
    +---> re-derive iph and uh from ip_hdr(skb)
```

**Checksum shortcut**: for IPv4 UDP, setting `uh->check = 0` is valid.
RFC 768 defines zero as "no checksum computed." This lets you rewrite
fields without recalculating. (TCP and IPv6 do not allow this.)

## Build it

1. Hook `NF_INET_LOCAL_OUT` (not PRE_ROUTING, since we are modifying
   outgoing packets from this host).
2. In the callback, check `iph->protocol == IPPROTO_UDP`.
3. Locate the UDP header after the IP header (same offset math as ling
   43's ICMP parsing).
4. Before modifying, call `skb_ensure_writable` up to the end of the UDP
   header. Cloned skbs share data; writing without this can corrupt other
   users of the same buffer. Re-derive `iph` and `uh` after the call
   (the skb data pointer may have changed).
5. If `uh->dest` matches the trap port, rewrite it to the redirect port.
6. Zero the UDP checksum so the rewritten packet is accepted.
6. Count redirections with an atomic counter.
7. `/proc/rk_mangler` serves the trap port, redirect port, and counter.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
nc -l -u -p 9999 -s 127.0.0.1 > /tmp/out &
echo "intercepted" | nc -u -w1 127.0.0.1 7777
cat /tmp/out                    # data sent to :7777 arrived at :9999
cat /proc/rk_mangler                 # redirected=1
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_mangler` contains `trap_port=7777`, `redir_port=9999`, and `redirected=N`.
- UDP data sent to port 7777 arrives at a listener on port 9999.
- `redirected` counter increments for each rewritten packet.
- `/proc/rk_mangler` disappears after unload.

## Verify

```bash
./rootkitlings check 51
```

The check starts a UDP listener on port 9999, sends data to port 7777,
and verifies the listener received it (proving the hook redirected it).

> **What a defender sees**: `ss -u` shows no listener on port 7777,
> yet traffic to it succeeds. A port scan would miss the real service.
> Network forensics (tcpdump on the host side) reveals the rewritten
> destination port.

See also: [Persistence](../../07_persistence/README.md).
