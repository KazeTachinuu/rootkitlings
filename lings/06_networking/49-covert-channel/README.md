# 49 - Covert channel

## What we are learning

An ICMP echo request carries arbitrary payload bytes. This lesson uses
that field as a small message transport and then shows exactly where the
message remains observable.

## What we are doing

Build on ling 45's ICMP trigger. Instead of just counting magic pings,
extract the payload bytes after the ICMP header and store the message.

```
  ICMP echo request
  +--------+--------+---------+-----+---------+
  | IP hdr | type=8 | id=DEAD | seq | PAYLOAD |
  +--------+--------+---------+-----+---------+
                       ^               ^
                  magic trigger    covert message
                  (ling 45)        (this ling)
```

## New concepts

You already know how to find the ICMP header (ling 45). The payload
starts right after `sizeof(struct icmphdr)`. Its length is:

```
payload_len = total_ip_len - ip_header_len - sizeof(icmphdr)
```

Use `ntohs(iph->tot_len)` for total IP length and `iph->ihl * 4` for
the IP header length. Validate both before deriving the payload length.

An `skb` may be non-linear. Read headers with `skb_header_pointer()` and
copy payload bytes with `skb_copy_bits()`; direct pointer arithmetic is not
a valid general packet parser.

**Concurrency**: the hook writes the message, the proc read serves it.
The hook can run in softirq context, where code must not sleep. Protect the
buffer with a spinlock. The proc reader takes a snapshot under the lock and
formats it after unlocking.

## Build it

1. Hook `PRE_ROUTING` and parse ICMP (ling 45 pattern).
2. Use magic ID `0xDEAD` (different from ling 45's `0x1337`).
3. When triggered, copy the payload into a kernel buffer.
4. Protect the buffer with a spinlock.
5. `/proc/rk_covert` serves the last received message (or `(none)` if
   nothing was received yet).

## Try it

```bash
./rootkitlings run --network
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
./rootkitlings check 49
```

The check sends normal pings (must NOT trigger), then sends ICMP echo
with ID 0xdead carrying a payload using the `icmp_send` tool, and
verifies the message was extracted.

> **What a defender sees**: packet capture outside the guest records the ICMP
> identifier and the message in plaintext. This transport is concealed only
> from tools that ignore ICMP payloads; it is not encrypted.

See also: [50 - Traffic counter](../50-traffic-counter/README.md).
