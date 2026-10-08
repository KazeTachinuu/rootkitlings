# 41 - Classify IPv4 traffic

The IPv4 `protocol` field identifies the next header, including TCP, UDP, and ICMP.

This module classifies validated packets and publishes separate TCP and UDP totals through `/proc/rk_sniffer`.

## Observe it

On the host, boot the exercise VM:

```sh
./rootkitlings run L41
```

Inside the VM, record a baseline and send one UDP datagram:

```sh
insmod hello.ko
cat /proc/rk_sniffer
echo test | nc -u -w1 127.0.0.1 9999
cat /proc/rk_sniffer
```

```text
before: tcp=N udp=M
after:  tcp=N udp=M+1
```

After the UDP send, `udp` is larger.

A normal ping is ICMP, so it changes neither published counter.

## Header access

```text
skb -> validated IPv4 header -> protocol
                                /      \
                              TCP      UDP
                               |        |
                          tcp_count  udp_count
```

Packet data in an `skb` may be non-linear.

`skb_header_pointer()` safely reads the fixed IPv4 header into the supplied fallback buffer when direct access is unavailable.

If the call returns `NULL`, the callback accepts the packet without classifying it.

The atomic counters allow concurrent hook invocations without sleeping.

## Implement it

The scaffold already validates `skb`, obtains the IPv4 header, and owns the hook and proc lifecycles.

In `nf_intercept()`, increment `tcp_count` when `iph->protocol` is `IPPROTO_TCP` and `udp_count` when it is `IPPROTO_UDP`.

Leave every packet accepted.

## Verify

```sh
./rootkitlings check L41
```

The verification generates real TCP and UDP traffic from measured baselines and requires only the matching counter to advance.

The module reports protocol totals, not payloads, addresses, ports, or an independent packet trace.
