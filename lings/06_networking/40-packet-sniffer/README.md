# 40 - Packet sniffer

## What we are learning

IP headers contain a `protocol` field that identifies the transport layer
(TCP, UDP, ICMP). A kernel-level sniffer sees all traffic before any
userspace tool like `tcpdump`.

## What we are doing

Extend the netfilter hook from ling 39 to parse each packet's IP header
and classify it by protocol, keeping separate counters for TCP and UDP.

## Includes you need

Everything from ling 39, plus the IP header:

```c
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
```

## The concept

```
  incoming packet (skb)
       |
       v
  +----+----+------+-----------+
  | IP hdr  | TCP  |  payload  |     <-- TCP packet
  +---------+------+-----------+
       |
       +-> iph->protocol == IPPROTO_TCP  -->  tcp_count++
       +-> iph->protocol == IPPROTO_UDP  -->  udp_count++
       +-> anything else                 -->  ignore
```

The new API: `ip_hdr(skb)` returns a pointer to the IP header from a
socket buffer. The `protocol` field identifies the transport layer.
Protocol constants (`IPPROTO_TCP`, `IPPROTO_UDP`) come from `linux/in.h`.

## Build it

Extend your netfilter hook from ling 39. In the callback, extract the
IP header and classify each packet by protocol. Use `atomic_t` counters
(ling 20) for safe concurrent updates.

## Output format

`/proc/rk_sniffer` should print:

```
tcp=N
udp=N
```

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_sniffer                         # tcp=0 udp=0
ping -c 2 127.0.0.1                     # ICMP (not TCP or UDP)
cat /proc/rk_sniffer                         # counters unchanged
echo test | nc -u -w1 127.0.0.1 9999   # UDP traffic
cat /proc/rk_sniffer                         # udp counter incremented
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_sniffer` contains `tcp=N` and `udp=N` counters.
- Both start at 0, incrementing as traffic is seen.
- TCP traffic increments `tcp=`, UDP traffic increments `udp=`.
- `/proc/rk_sniffer` disappears after unload.

## Verify

```bash
./rootkitlings check 40
```

> **What a defender sees**: `dmesg` shows the sniffer activation.
> A module counting packets by protocol is a strong indicator of
> kernel-level traffic monitoring.

See also: [41 - ICMP trigger](../41-icmp-trigger/README.md).
