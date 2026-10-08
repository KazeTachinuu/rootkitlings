# 44 - Redirect an outgoing UDP packet

A Netfilter hook at `NF_INET_LOCAL_OUT` sees locally generated packets before routing completes.

This module rewrites UDP destination port `7777` to `9999` while preserving the payload.

## Observe it

On the host, boot the exercise VM:

```sh
./rootkitlings run L44
```

Inside the VM, start a listener on port `9999`, then send to `7777`:

```sh
insmod hello.ko
nc -u -l -p 9999 > /tmp/received &
echo nightfall | nc -u -w1 127.0.0.1 7777
cat /tmp/received
cat /proc/rk_redirect
```

```text
nightfall
redirected=1
redirect_port=9999
```

## Rewrite point

```text
sender -> UDP :7777 -> LOCAL_OUT -> UDP :9999 -> listener
                              |
                         rewrite dest
```

An `skb` may share its packet storage.

`skb_ensure_writable()` makes the required header range private and writable.

Header pointers must be reacquired afterward because the operation may move the packet data.

The scaffold rejects fragments and validates both declared packet lengths before reading the UDP header.

It then matches port `7777`, makes the header writable, and reacquires its pointer.

Changing a checksummed field requires an incremental checksum update.

`inet_proto_csum_replace2()` preserves a complete software checksum and cooperates with checksum offload represented by `CHECKSUM_PARTIAL`.

An original checksum of zero remains zero because IPv4 UDP uses that value to mean that no checksum was supplied.

## Implement it

Assign the prepared network-order value `new_port` to `writable_udph->dest` at the marked line.

Do not move the assignment before `skb_ensure_writable()` and do not use the old header pointer after that call.

The validated lengths and checksum update are provided so the exercise remains focused on the writable-header rewrite.

## Verify

```sh
./rootkitlings check L44
```

The verification sends a runtime payload to `7777`, requires the exact bytes to arrive at `9999`, and observes the redirect counter.

It also proves that traffic for another UDP port passes through without changing the counter, then confirms cleanup.

Packet capture sees destination port `9999`, revealing the rewritten packet independently of the sender's request.
