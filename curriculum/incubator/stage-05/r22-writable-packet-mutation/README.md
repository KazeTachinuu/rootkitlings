# Mutate a writable packet and repair checksum state

L20 gave you a validated `rk_udp_view` without changing the packet.
This exercise uses that view to replace one UDP destination port safely.

Edit one file:

- `mutation/udp_redirect.c`

Run:

```bash
./rootkitlings check
```

The supplied IPv4 `LOCAL_OUT` hook handles only traffic in the initial network namespace whose destination is exactly `127.0.0.1`.
It classifies the packet with the accepted L17-L20 code, calls your mutator for the configured match port, and always returns `NF_ACCEPT`.

This is a controlled one-way rewrite.
It is not NAT: there is no reverse translation, route change, address change, or general conntrack support.

## Validate before writing

`view` must describe one complete UDP datagram:

- source and destination ports fit in 16 bits;
- its destination equals `match_port`;
- its datagram is at least one UDP header;
- payload offset and length follow from the datagram offset and length without arithmetic overflow.

Reject inconsistent `sk_buff` lengths, negative or overflowing offsets, an offset outside the `int` range accepted by `skb_copy_bits()`, and a datagram that extends beyond `skb->len`.
Use `skb_copy_bits()` to prove the UDP header is readable before requesting a write.

Return `RK_UDP_REDIRECT_UNSUPPORTED_STATE` for a packet with conntrack or XFRM state.
GSO is supported only with coherent `CHECKSUM_PARTIAL`.
A partial checksum must start at the UDP header, place its result in `udphdr.check`, and use the Internet checksum rather than CRC32c.

## Make the exact prefix writable

Call `rk_skb_make_writable()` with the byte count from `skb->data` through the end of the UDP header.
The helper can linearize, unshare, or relocate storage.
After it returns, recompute the network and UDP offsets and reacquire the UDP pointer.
Do not keep a pointer taken before the call.

The helper may change packet geometry even when it reports failure.
On every failure, leave the UDP header bytes unchanged.

Before writing, compare the live source port, destination port, and UDP length with both the pre-helper header and the validated view.
Return `RK_UDP_REDIRECT_STALE_VIEW` if any differ.

## Repair derived state

For a nonzero UDP checksum, adjust it for the destination-port change:

```c
inet_proto_csum_replace2(&udp->check, skb, old_port, new_port, false);
```

If that adjustment produces zero, store `CSUM_MANGLED_0`.

For an original zero checksum, call `skb_checksum_complete_unset()` and leave the checksum field zero.
This includes `CHECKSUM_PARTIAL`: a zero partial seed must not become `CSUM_MANGLED_0`.

Then write the new destination port and clear the packet hash.
There must be no fallible operation after the first checksum or header change.

## What the checker covers

The host checker runs the learner file under AddressSanitizer and UndefinedBehaviorSanitizer.
It covers offset and `size_t` overflow, linear, nonlinear, shared, unaligned, and relocating storage, all checksum modes, zero and nonzero checksum fields, coherent and incoherent GSO, conntrack and XFRM rejection, stale views, exact writable length, helper failure after a geometry change, header rollback, and hash invalidation.
Its checksum model matches Linux: the helper does not change a nonzero `CHECKSUM_PARTIAL` seed when `pseudohdr` is false.

The Linux 7.2 guest repeats the mutation with real linear, page-fragmented, `frag_list`, and cloned `sk_buff` objects.
It then proves an exact binary UDP payload sent to the match port arrives at the replacement port, while unmatched, IPv6, alternate-address, and child-network-namespace traffic remains unchanged.
It also checks concurrent traffic, rollback, unload, and reload.

## References

- [Linux v7.2 `sk_buff` checksum and writable-buffer APIs](https://github.com/torvalds/linux/blob/v7.2/include/linux/skbuff.h)
- [Linux v7.2 `skb_ensure_writable()` implementation](https://github.com/torvalds/linux/blob/v7.2/net/core/skbuff.c)
- [Linux v7.2 Internet checksum replacement](https://github.com/torvalds/linux/blob/v7.2/net/core/utils.c)
- [Linux v7.2 Netfilter UDP port rewrite](https://github.com/torvalds/linux/blob/v7.2/net/netfilter/nf_nat_proto.c)
- [Linux v7.2 UDP GSO checksum handling](https://github.com/torvalds/linux/blob/v7.2/net/ipv4/udp_offload.c)
