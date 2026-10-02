# 41 - Netfilter hook

## What we are learning

Netfilter is the kernel's packet processing framework. Every IPv4 packet
passes through a chain of hook points. By registering a callback at one
of those points, a module can inspect, modify, or drop any packet.

```
  incoming packet
       |
  NF_INET_PRE_ROUTING   <-- hook here
       |
  routing decision
      / \
     /   \
  LOCAL_IN   FORWARD
  (for us)   (other host)
     |          |
  local app  POST_ROUTING
                |
              wire
```

This is the entry point for everything in the networking section.
You will use this pattern in every ling from here to 47.

## What we are doing

Register a callback at `NF_INET_PRE_ROUTING` that counts every IPv4
packet. Expose the running count through `/proc/rk_nfhook`.

## New API

Two new headers:

```c
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
```

The hook is described by a struct. Fill in these four fields:

```c
static struct nf_hook_ops nf_ops = {
    .hook     = ...,           // your callback
    .pf       = NFPROTO_IPV4,  // protocol family
    .hooknum  = NF_INET_PRE_ROUTING,
    .priority = NF_IP_PRI_FIRST,
};
```

The callback receives a socket buffer (`struct sk_buff *skb`) containing
the packet. Return `NF_ACCEPT` to let it through, or `NF_DROP` to
discard it.

Registration and cleanup:

```c
nf_register_net_hook(&init_net, &nf_ops);
nf_unregister_net_hook(&init_net, &nf_ops);
```

Use `atomic_t` for the counter. Hooks run in softirq context (no
sleeping, concurrent execution on multiple CPUs). You already used
atomics in ling 22.

Serve the count through `/proc/rk_nfhook` (ling 10 pattern).

## Hints

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_nfhook                 # packet count starts at 0
ping -c 3 127.0.0.1             # generate some traffic
cat /proc/rk_nfhook                 # counter should have increased
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_nfhook` returns the packet counter as a plain number.
- The counter increments when packets traverse the hook (e.g., `ping 127.0.0.1`).
- `/proc/rk_nfhook` disappears after unload.

## Verify

```bash
./rootkitlings check 39
```

The check brings up loopback, pings localhost, and verifies the counter
incremented.

> **What a defender sees**: `dmesg` shows the hook registration message.
> `cat /proc/rk_nfhook` reveals the packet count. Netfilter hooks are visible
> via `/proc/net/netfilter/` interfaces.

See also: [40 - Packet sniffer](../40-packet-sniffer/README.md).
