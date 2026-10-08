# 42 - Match an ICMP trigger

An ICMP echo request contains a 16-bit identifier used to distinguish echo streams.

This module treats identifier `0x1337` as a trigger and counts matching requests without dropping them.

## Observe it

On the host, boot the exercise VM:

```sh
./rootkitlings run L42
```

Inside the VM, send one ordinary identifier and one trigger identifier:

```sh
insmod hello.ko
icmp_send 127.0.0.1 1234
cat /proc/rk_trigger
icmp_send 127.0.0.1 1337
cat /proc/rk_trigger
```

```text
triggers=1
magic=0x1337
```

The non-matching request leaves `triggers` unchanged.

## Locate the identifier

```text
IPv4 header                 ICMP echo header
+----------------------+    +------+------+----+-----+
| ihl | protocol=ICMP  |--->| type | code | id | seq |
+----------------------+    +------+------+----+-----+
                                      0x1337
```

The IPv4 header length is `iph->ihl * 4` bytes, so the ICMP header does not always begin after a fixed 20-byte offset.

The scaffold validates the IPv4 version, header length, protocol, fragmentation state, total length, and both header reads.

Any 16-bit identifier can occur in ordinary traffic.

The value is a recognizable signal, not authentication.

## Implement it

Set `matched` to the result of one predicate:

```c
icmph->type == ICMP_ECHO &&
ntohs(icmph->un.echo.id) == MAGIC_ID
```

The supplied `if (matched)` block increments `trigger_count` and logs the trigger.

Do not change the packet verdict or parsing path.

## Verify

```sh
./rootkitlings check L42
```

The verification sends explicit non-magic and magic echo requests and proves that only the latter changes the live counter.

Packet capture exposes the identifier independently of `/proc/rk_trigger`.
