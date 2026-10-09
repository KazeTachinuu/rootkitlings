# Parse packet lengths before touching payload

An IPv4 packet carries two different bounds:

- `available` is the number of bytes the caller can safely read.
- The header's Total Length is the number of bytes the packet declares.

Neither bound proves the other. A parser must validate both before it reports where a transport header or payload begins.

## The fields used here

The first byte contains the four-bit version followed by the four-bit Internet Header Length, or IHL. IHL counts 32-bit words, so a value of 5 means 20 bytes and a value of 15 means 60 bytes. Values below 5 are malformed IPv4 headers.

Total Length is a two-byte, big-endian value at bytes 2 and 3. It includes the IPv4 header. A declared length smaller than the header is malformed. A declared length larger than `available` is truncated. Extra available bytes after Total Length are outside this packet and must not enlarge the transport view.

The two-byte fragment field is also big-endian:

- The top bit is reserved and must be zero.
- `0x4000` is Don't Fragment. It does not mean that the packet is a fragment.
- `0x2000` is More Fragments.
- The low 13 bits are the fragment offset.

This parser rejects More Fragments and every nonzero fragment offset. Later code can therefore inspect a complete transport header without accidentally interpreting a later fragment as its beginning. Reassembly is a different mechanism and is outside this exercise.

## Your task

Edit one file:

- `parser/ipv4.c`

Implement `rk_parse_ipv4()`.

On success, fill the output view with:

- the validated IPv4 header length;
- the declared packet length;
- the transport offset and remaining transport length;
- the protocol byte.

Return the exact failure class:

- `RK_IPV4_PARSE_TRUNCATED` when the supplied bytes end before a required declared boundary;
- `RK_IPV4_PARSE_MALFORMED` when present fields contradict the IPv4 format;
- `RK_IPV4_PARSE_FRAGMENTED` for a well-bounded fragment that this parser deliberately declines;
- `RK_IPV4_PARSE_OK` for one complete, unfragmented packet.

Do not change the output view on failure. Parse the two-byte fields from individual bytes; the input pointer need not be aligned. Validate a larger value before subtracting a smaller one.

Run:

```bash
./rootkitlings check
```

The checker compiles the exact parser for the pinned kernel and separately runs it with address and undefined-behavior sanitizers. Its corpus covers every IHL byte, every 16-bit Total Length, every 16-bit fragment field, unaligned input, truncation, options, capture padding, and all protocol values.

This exercise does not validate the IPv4 checksum or interpret options, addresses, TTL, or transport bytes. It establishes only the boundaries that later packet code may safely use.

## References

- [RFC 791: IPv4 header format, IHL, Total Length, flags, and fragment offset](https://www.rfc-editor.org/rfc/rfc791.html#section-3.1)
- [Linux v7.2 `struct iphdr`](https://github.com/torvalds/linux/blob/v7.2/include/uapi/linux/ip.h)
- [Linux v7.2 fragment test](https://github.com/torvalds/linux/blob/v7.2/include/net/ip.h#L406-L409)
