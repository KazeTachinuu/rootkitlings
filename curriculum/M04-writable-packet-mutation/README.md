# Redirect one UDP signal to a new port

The traffic watch has recognized a target UDP datagram.
Redirect that one datagram to a different local port while allowing all local
IPv4 traffic to continue.

Success proves that you can make shared packet storage writable, reject a stale
classification, and repair checksum state after changing the header.

The packet classifier supplies a validated `rk_udp_view` without changing the
packet.
This exercise uses that view to replace one UDP destination port safely.

Edit one file:

- `mutation/udp_redirect.c`

Run:

```sh
./rootkitlings check
```

The supplied `LOCAL_OUT` hook handles only `init_net` IPv4 to `127.0.0.1`.
It uses the supplied parser, access, observer, and classifier components, calls
your mutator for the match port, and always returns `NF_ACCEPT`.

This one-way rewrite is not NAT and has no reverse translation or route change.

## Validate before writing

`view` must describe one complete UDP datagram:

- source and destination ports fit in 16 bits;
- its destination equals `match_port`;
- its datagram is at least one UDP header;
- payload offset and length follow from the datagram offset and length without
  arithmetic overflow.

Reject inconsistent `sk_buff` lengths, negative or overflowing offsets, an
offset outside the `int` range accepted by `skb_copy_bits()`, and a datagram
that extends beyond `skb->len`.
Use `skb_copy_bits()` to prove the UDP header is readable before requesting a
write.

Return `RK_UDP_REDIRECT_UNSUPPORTED_STATE` for a packet with conntrack or XFRM
state.
GSO is supported only with coherent `CHECKSUM_PARTIAL`.
A partial checksum must start at the UDP header, place its result in
`udphdr.check`, and use the Internet checksum rather than CRC32c.

## Make the exact prefix writable

Call `rk_skb_make_writable()` with the byte count from `skb->data` through the
end of the UDP header.
The helper may linearize, unshare, or relocate storage.
Afterward, recompute offsets and reacquire the UDP pointer.
Do not keep a pointer taken before the call.

The helper may change packet geometry even when it reports failure.
On every failure, leave the UDP header bytes unchanged.

Before writing, compare the live source port, destination port, and UDP length
with both the pre-helper header and the validated view.
Return `RK_UDP_REDIRECT_STALE_VIEW` if any differ.

## Repair derived state

For a nonzero UDP checksum, adjust it for the destination-port change:

```c
inet_proto_csum_replace2(&udp->check, skb, old_port, new_port, false);
```

If that adjustment produces zero, store `CSUM_MANGLED_0`.

For an original zero checksum, call `skb_checksum_complete_unset()` and leave
the checksum field zero.
This includes `CHECKSUM_PARTIAL`: a zero partial seed must not become
`CSUM_MANGLED_0`.

Then write the new destination port and clear the packet hash.
There must be no fallible operation after the first checksum or header change.

## What the checker covers

Sanitized checks cover arithmetic, storage forms, checksum modes, GSO,
unsupported state, stale views, helper failure, rollback, and hash invalidation.
The Linux guest repeats real fragmented and cloned buffers, verifies exact
delivery and controls, and checks concurrent traffic, unload, and reload.

## References

- [Linux v7.2 `sk_buff` checksum and writable-buffer APIs][1]
- [Linux v7.2 `skb_ensure_writable()` implementation][2]
- [Linux v7.2 Internet checksum replacement][3]
- [Linux v7.2 Netfilter UDP port rewrite][4]
- [Linux v7.2 UDP GSO checksum handling][5]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/linux/skbuff.h
[2]: https://github.com/torvalds/linux/blob/v7.2/net/core/skbuff.c
[3]: https://github.com/torvalds/linux/blob/v7.2/net/core/utils.c
[4]: https://github.com/torvalds/linux/blob/v7.2/net/netfilter/nf_nat_proto.c
[5]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/udp_offload.c
