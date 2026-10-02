# 47 - Covert channel

## What we are learning

Rootkits communicate with C2 servers through covert channels embedded
in legitimate protocols. ICMP echo payloads are a classic vehicle:
firewalls rarely inspect ping content, and the payload bytes carry
hidden commands.

This technique is used by Pingback (2021), Regin, and many APT tools.

## What we are doing

Build on ling 43's ICMP trigger. Instead of just counting magic pings,
extract the payload bytes after the ICMP header and store the message.

```
  ICMP echo request
  +--------+--------+---------+-----+---------+
  | IP hdr | type=8 | id=DEAD | seq | PAYLOAD |
  +--------+--------+---------+-----+---------+
                       ^               ^
                  magic trigger    covert message
                  (ling 43)        (this ling)
```

## New concepts

You already know how to find the ICMP header (ling 43). The payload
starts right after `sizeof(struct icmphdr)`. Its length is:

```
payload_len = total_ip_len - ip_header_len - sizeof(icmphdr)
```

Use `ntohs(iph->tot_len)` for total IP length and `iph->ihl * 4` for
the IP header length. Both are values you have used before.

**Concurrency**: the hook writes the message, the proc read serves it.
These run in different contexts. Protect the shared buffer with
`DEFINE_MUTEX()` and `mutex_lock`/`mutex_unlock` (ling 21 pattern).

## Build it

1. Hook `PRE_ROUTING` and parse ICMP (ling 43 pattern).
2. Use magic ID `0xDEAD` (different from ling 43's `0x1337`).
3. When triggered, copy the payload into a kernel buffer.
4. Protect the buffer with a mutex.
5. `/proc/rk_covert` serves the last received message (or `(none)` if
   nothing was received yet).

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_covert                 # msg=(none)
ping -c 1 127.0.0.1             # normal ping, no trigger
cat /proc/rk_covert                 # msg still (none)
icmp_send 127.0.0.1 dead "secret_orders"
cat /proc/rk_covert                 # msg=secret_orders
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_covert` contains `magic=0xdead` and `msg=`.
- `msg` shows the last covert message, or `(none)` initially.
- Normal ping does NOT trigger the channel.
- ICMP echo with ID 0xdead extracts the payload into `msg`.
- `/proc/rk_covert` disappears after unload.

## Verify

```bash
./rootkitlings check 45
```

The check sends normal pings (must NOT trigger), then sends ICMP echo
with ID 0xdead carrying a payload using the `icmp_send` tool, and
verifies the message was extracted.

> **What a defender sees**: ICMP packets with unusual payload sizes or
> patterns are flagged by IDS. Deep packet inspection on ICMP reveals
> embedded data.

See also: [46 - Traffic counter](../46-traffic-counter/README.md).
