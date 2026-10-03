# 45 - ICMP trigger

## What we are learning

An ICMP echo request carries an identifier field. This lesson treats one value
as a command trigger, then counts matching packets. Packet capture and network
monitoring can read the same field, so the trigger is not hidden on the wire.

## What we are doing

Hook `NF_INET_PRE_ROUTING` (same pattern as ling 43), parse ICMP echo
requests, and trigger on packets whose echo ID matches `0x1337`. Count
activations and expose state through `/proc/rk_trigger`.

## ICMP echo anatomy

An ICMP echo request sits right after the IP header:

```
  IP header (variable length)     ICMP header (8 bytes)
  +---------------------------+  +------+------+----+-----+
  | ... | protocol=1 | ihl=N |  | type | code | id | seq |
  +---------------------------+  +------+------+----+-----+
                                   8      0    magic  ...
                                  (echo)       here
```

The `id` field commonly distinguishes concurrent ping sessions. We use
`0x1337` as the exercise value; ordinary traffic can still collide with any
16-bit value, so the value is not authentication.

## New API

Two new headers (IP and ICMP structures):

```c
#include <linux/ip.h>
#include <linux/icmp.h>
```

Key types and functions:
- `struct iphdr`: has `protocol` (check for `IPPROTO_ICMP`) and `ihl`
  (header length in 32-bit words)
- `struct icmphdr`: has `type` (check for `ICMP_ECHO`),
  `un.echo.id` (the magic field)
- `ip_hdr(skb)`: extracts the IP header from a socket buffer
- `ntohs()`: converts the echo ID from network to host byte order

## Build it

1. Set up a netfilter hook at `PRE_ROUTING` (ling 43 pattern).
2. In the callback, get the IP header with `ip_hdr(skb)`.
3. Skip non-ICMP packets (`iph->protocol != IPPROTO_ICMP`).
4. Locate the ICMP header. It starts right after the IP header.
   The IP header is `iph->ihl * 4` bytes long.
5. Check `type == ICMP_ECHO` and `ntohs(un.echo.id) == 0x1337`.
6. If both match, increment an atomic counter.
7. Expose `triggers=N` and `magic=0x1337` via `/proc/rk_trigger`.

## Try it

```bash
./rootkitlings run --network
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_trigger                 # triggers=0
icmp_send 127.0.0.1 1234        # controlled non-magic echo ID
cat /proc/rk_trigger                 # triggers still 0
icmp_send 127.0.0.1 1337        # magic echo ID
cat /proc/rk_trigger                 # triggers=1
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_trigger` contains `magic=0x1337` and `triggers=N`.
- `triggers` starts at 0, increments only for ICMP echo with ID 0x1337.
- An echo request with a controlled non-magic ID does not trigger the counter.
- `/proc/rk_trigger` disappears after unload.

## Verify

```bash
./rootkitlings check 45
```

The check sends explicit non-magic and magic echo IDs with `icmp_send` and
verifies that only `0x1337` increments the counter.

> **What a defender sees**: `dmesg` shows the hook registration.
> Unusual ICMP filtering logic in a loaded module is suspicious.
> Network IDS can flag ICMP packets with non-standard echo IDs.

See also: [46 - Raw socket](../46-raw-socket/README.md).
