# Recognize one UDP signal without changing it

The outbound watch sees every local IPv4 packet.
Select only a structurally valid UDP datagram addressed to one chosen port, and
leave every packet untouched.

Success proves that packet access, IPv4 validation, and UDP classification can
form one strict inspection path.
It recognizes structure only; it does not inspect a message or prove delivery.

This exercise joins three already-tested pieces:

- the bounded IPv4 parser validates the header and rejects fragments;
- the bounded `sk_buff` reader copies packet bytes safely;
- the Netfilter observer runs the inspection from an IPv4 `LOCAL_OUT` hook.

Classify a separate eight-byte transport header from a validated IPv4 view
without touching the packet.

`ipv4` must be an unchanged successful parser result.
It guarantees a 20-to-60-byte header, `header_length == payload_offset`, and
`payload_length == packet_length - header_length` within 16 bits.
Do not repeat those checks.

## UDP boundaries

UDP Length includes its eight-byte header and cannot exceed IPv4 payload length.

Do not require equal lengths.
Report bytes after UDP but inside IPv4 as `trailing_length`, the surplus area.

`udp_header_length` is only the supplied buffer size, not packet length.
Integration passes exactly `sizeof(udp_header)`.

Read the source port, destination port, and UDP length byte by byte in network
byte order.
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

Ignore checksum state because offload may be pending at `LOCAL_OUT`.
A match proves structure only, not delivery or final wire form.

## Your task

Edit one file:

- `classifier/udp.c`

Implement `rk_classify_ipv4_udp()`.

Run:

```bash
./rootkitlings check
```

Sanitized checks cover every 16-bit UDP field, unaligned input, precedence,
guarded non-UDP input, and unchanged failure outputs.
Guest checks cover options, surplus, GSO, fragments, namespace controls,
rollback, concurrent traffic, unload, and reload.

## References

- [RFC 768: UDP header fields and length][1]
- [RFC 9868: UDP Length and the surplus area][2]
- [Linux v7.2 IPv4 UDP receive length handling][3]
- [Linux v7.2 IPv4 local-output path][4]

[1]: https://www.rfc-editor.org/rfc/rfc768.html
[2]: https://www.rfc-editor.org/rfc/rfc9868.html#section-7
[3]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/udp.c
[4]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/ip_output.c
