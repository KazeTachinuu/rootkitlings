# 41 - ICMP trigger

## What we are learning

Many real rootkits (Azazel, Reptile, Bdvl) use specially crafted ICMP
echo requests as a covert command channel. A ping with a specific ID
field triggers an action: reverse shell, privilege escalation, or
configuration change.

Firewalls rarely block ICMP echo, and the magic ID is invisible to
anyone not inspecting raw headers.

## What we are doing

Hook `NF_INET_PRE_ROUTING` (same pattern as ling 39), parse ICMP echo
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

The `id` field is what `ping` sets to its PID. We pick a magic value
(`0x1337`) that normal pings will never use.

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

1. Set up a netfilter hook at `PRE_ROUTING` (ling 39 pattern).
2. In the callback, get the IP header with `ip_hdr(skb)`.
3. Skip non-ICMP packets (`iph->protocol != IPPROTO_ICMP`).
4. Locate the ICMP header. It starts right after the IP header.
   The IP header is `iph->ihl * 4` bytes long.
5. Check `type == ICMP_ECHO` and `ntohs(un.echo.id) == 0x1337`.
6. If both match, increment an atomic counter.
7. Expose `triggers=N` and `magic=0x1337` via `/proc/rk_trigger`.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_trigger                 # triggers=0
ping -c 2 127.0.0.1             # normal ping (random echo ID)
cat /proc/rk_trigger                 # triggers still 0
icmp_send 127.0.0.1 1337        # magic echo ID
cat /proc/rk_trigger                 # triggers=1
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_trigger` contains `magic=0x1337` and `triggers=N`.
- `triggers` starts at 0, increments only for ICMP echo with ID 0x1337.
- Normal `ping` does NOT trigger the counter.
- `/proc/rk_trigger` disappears after unload.

## Verify

```bash
./rootkitlings check 41
```

The check sends normal pings (must NOT trigger), then sends ICMP echo
requests with ID 0x1337 using the `icmp_send` tool and verifies the
counter increments.

> **What a defender sees**: `dmesg` shows the hook registration.
> Unusual ICMP filtering logic in a loaded module is suspicious.
> Network IDS can flag ICMP packets with non-standard echo IDs.

See also: [42 - Raw socket](../42-raw-socket/README.md).
