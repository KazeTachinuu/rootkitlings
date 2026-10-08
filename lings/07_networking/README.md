# Networking

These lings move from observing packets to creating sockets and encoding a DNS name.

Every mechanism remains observable through another view: registered hooks, socket tables, packet capture, or protocol structure.

| # | Mechanism | Observable result | Important limit |
|--:|-----------|-------------------|-----------------|
| 40 | Netfilter hook | A counter advances when IPv4 packets cross `PRE_ROUTING`. | The registered hook is discoverable. |
| 41 | Protocol counter | Real TCP and UDP traffic advances separate counters. | It classifies headers; it does not capture payloads. |
| 42 | ICMP trigger | Only echo requests with ID `0x1337` advance the trigger count. | A fixed 16-bit value is not authentication. |
| 43 | Raw socket | One kernel-owned ICMP socket appears in `/proc/net/raw`. | No packet is sent in this exercise. |
| 44 | UDP redirect | A dynamic payload sent to port 7777 arrives unchanged on port 9999. | Packet capture exposes the rewritten flow. |
| 45 | TCP root shell | A listening kernel socket hands an accepted connection to an interactive privileged shell. | It is unauthenticated; the listener, shell, and command side effects remain visible. |
| 46 | ICMP payload | A magic echo request transfers one bounded plaintext message. | Packet capture sees the identifier and message. |
| 47 | DNS wire format | A dotted name survives bounded encode/decode while malformed labels fail. | It does not send or resolve DNS. |

```text
packet path             socket path                 protocol bytes
-----------             -----------                 --------------
40 hook                 43 raw socket               47 DNS labels
   |                    45 TCP listener
   +--> 41 classify
   +--> 42 trigger
   +--> 44 rewrite
   +--> 46 payload
```

After ling 47, [Connect-back task agent](../../capstones/7-implant/README.md) combines a kernel socket, worker lifecycle, reconnection, and a fixed command protocol.
