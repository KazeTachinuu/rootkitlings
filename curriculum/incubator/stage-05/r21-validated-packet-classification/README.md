# Classify one bounded IPv4 UDP datagram

This exercise joins three already-tested pieces:

- L17 validates the IPv4 header and rejects fragments;
- L18 copies packet bytes through a bounded `sk_buff` reader;
- L19 runs the inspection from an IPv4 `LOCAL_OUT` hook.

Your classifier receives the validated IPv4 view and a separate buffer containing the first eight bytes of its transport payload.
It decides whether those bytes are a complete UDP header for the configured destination port.
It does not touch the packet.

`ipv4` must be an unchanged view returned by a successful L17 parser call.
That precondition guarantees a 20-to-60-byte header, equal `header_length` and `payload_offset`, a 16-bit-bounded `packet_length`, and `payload_length == packet_length - header_length`.
The classifier does not duplicate those IPv4 checks.

## UDP boundaries

The UDP Length field includes the eight-byte UDP header.
A valid length is therefore at least eight bytes, and it cannot exceed the validated IPv4 payload length.

Do not require the two lengths to be equal.
Bytes between the end of the UDP datagram and the end of the IPv4 payload are reported as `trailing_length`.
They are outside the UDP datagram.
RFC 9868 calls this the UDP surplus area.

`udp_header_length` is only the size of the supplied header buffer.
It is not the UDP datagram length and it is not the remaining length of the `sk_buff`.
The supplied integration copies exactly eight bytes and passes `sizeof(udp_header)`.

Read the source port, destination port, and UDP length byte by byte in network byte order.
Do not cast the byte buffer to `struct udphdr`: the buffer may be unaligned.

## Result order

Apply these checks in order:

1. Return `RK_UDP_MALFORMED` when the IPv4 view or output view is null, or when
   `target_port` is greater than 65535.
2. Return `RK_UDP_OTHER_PROTOCOL` when the validated IPv4 protocol is not UDP.
   Do not inspect the UDP buffer in this case.
3. Return `RK_UDP_MALFORMED` when the IPv4 payload is shorter than eight bytes.
4. Return `RK_UDP_MALFORMED` when the UDP buffer is null.
5. Return `RK_UDP_TRUNCATED` when that buffer contains fewer than eight bytes.
6. Decode the three 16-bit fields needed by this exercise.
7. Return `RK_UDP_MALFORMED` when UDP Length is below eight or above the IPv4
   payload length.
8. Return `RK_UDP_OTHER_PORT` when the destination port does not match.
9. Fill a local `struct rk_udp_view`, copy it to `*view`, and return
   `RK_UDP_MATCH`.

Leave `*view` unchanged for every result except `RK_UDP_MATCH`.

The checksum field is outside this classifier's contract.
At `LOCAL_OUT`, checksum offload work may still be pending.
A structural match does not claim a valid checksum, successful delivery, one userspace send, or one final wire packet.

## Your task

Edit one file:

- `classifier/udp.c`

Implement `rk_classify_ipv4_udp()`.

Run:

```bash
./rootkitlings check
```

The checker accepts equivalent bounded C implementations.
Every non-null IPv4 view in the corpus is produced by the accepted L17 parser.
The host corpus exercises every 16-bit UDP length, source port, and destination port under AddressSanitizer and UndefinedBehaviorSanitizer.
It checks unaligned input and unchanged failure outputs, and rejects implementations that confuse the eight-byte buffer extent with a packet length.
The disposable Linux 7.2 guest also covers IPv4 options, malformed lengths, surplus bytes, checksum-independent classification, UDP GSO, fragments, concurrent status reads, rollback, unload during traffic, and reload.

## References

- [RFC 768: UDP header fields and length](https://www.rfc-editor.org/rfc/rfc768.html)
- [RFC 9868: UDP Length and the surplus area](https://www.rfc-editor.org/rfc/rfc9868.html#section-7)
- [Linux v7.2 IPv4 UDP receive length handling](https://github.com/torvalds/linux/blob/v7.2/net/ipv4/udp.c)
- [Linux v7.2 IPv4 local-output path](https://github.com/torvalds/linux/blob/v7.2/net/ipv4/ip_output.c)
