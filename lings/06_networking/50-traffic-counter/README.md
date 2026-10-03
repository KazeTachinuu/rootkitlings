# 50 - Traffic counter

## What we are learning

So far each ling has registered a single netfilter hook. The kernel
provides `nf_register_net_hooks` (plural) to register an array of hooks
in one call. This ling hooks two points to count traffic in both
directions.

```
  incoming                     outgoing
     |                            |
  PRE_ROUTING                 POST_ROUTING
     |                            |
  rx_packets++                tx_packets++
  rx_bytes += pkt_len         tx_bytes += pkt_len
     |                            |
     v                            v
  local app  <-- loopback -->  local app
```

On loopback, a single ping generates both RX and TX traffic because the
packet traverses both hook points.

## New API

Register multiple hooks at once with an array:

```c
static struct nf_hook_ops nf_ops[] = { ... , ... };
```

Register and unregister the entire array:

```c
nf_register_net_hooks(&init_net, nf_ops, ARRAY_SIZE(nf_ops));
nf_unregister_net_hooks(&init_net, nf_ops, ARRAY_SIZE(nf_ops));
```

For byte counters, use `atomic64_t` (a single large packet can carry up
to 65535 bytes). Get the packet size with `ntohs(ip_hdr(skb)->tot_len)`.

## Build it

1. Define two hook callbacks: one for `NF_INET_PRE_ROUTING` (RX), one
   for `NF_INET_POST_ROUTING` (TX).
2. Each callback increments a packet counter (`atomic_t`) and adds the
   packet length to a byte counter (`atomic64_t`).
3. Register both in a `nf_hook_ops` array (ling 43 pattern, extended).
4. `/proc/rk_counter` serves all four counters.

## Try it

```bash
./rootkitlings run --network
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_counter                 # rx_packets=0, tx_packets=0
ping -c 3 127.0.0.1             # loopback traffic
cat /proc/rk_counter                 # counters increased
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_counter` contains `rx_packets=`, `tx_packets=`, `rx_bytes=`, and `tx_bytes=`.
- All four counters increment when traffic flows (e.g., `ping 127.0.0.1`).
- `/proc/rk_counter` disappears after unload.

## Verify

```bash
./rootkitlings check 50
```

The check pings localhost and verifies all four counters incremented.

> **What a defender sees**: a module hooking both PRE and POST routing
> is monitoring all traffic directions. The counters reveal whether
> traffic is flowing.

See also: [51 - Packet mangling](../51-packet-mangling/README.md).
