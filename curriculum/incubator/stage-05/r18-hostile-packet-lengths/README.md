# Parse packet lengths before touching payload

An IPv4 packet carries two different bounds:

- `available` is the number of bytes the packet's backing storage can provide through its safe-copy interface.
- The header's Total Length is the number of bytes the packet declares.

Neither bound proves the other.
A parser must validate both before it reports where the IPv4 payload begins.

## The fields used here

The first byte contains the four-bit version followed by the four-bit Internet Header Length, or IHL.
IHL counts 32-bit words, so a value of 5 means 20 bytes and a value of 15 means 60 bytes.
Values below 5 are malformed IPv4 headers.

Total Length is a two-byte, big-endian value at bytes 2 and 3.
It includes the IPv4 header.
A declared length smaller than the header is malformed.
A declared length larger than `available` is truncated.
Extra available bytes after Total Length are outside this packet and must not enlarge the payload view.

The two-byte fragment field is also big-endian:

- RFC 791 reserves the top bit and says senders must set it to zero.
- RFC 1812 says a router must not reject a packet solely because that bit is nonzero.
- Linux v7.2 names the bit `IP_CE` and excludes it from the fragment test.
- This receive-side parser follows the Linux policy and ignores it.
- `0x4000` is Don't Fragment.
  It does not mean that the packet is a fragment.
- `0x2000` is More Fragments.
- The low 13 bits are the fragment offset.

This parser rejects More Fragments and every nonzero fragment offset.
Later code can therefore treat the reported offset as the start of a complete datagram payload instead of mistaking a later fragment for its beginning.
Reassembly is a different mechanism and is outside this exercise.

## Your task

Edit one file:

- `parser/ipv4.c`

Implement `rk_parse_ipv4()`.

On success, fill the output view with:

- the validated IPv4 header length;
- the declared packet length;
- the payload offset and remaining payload length;
- the protocol byte.

Return the exact failure class:

- `RK_IPV4_PARSE_TRUNCATED` when the supplied bytes end before a required declared boundary;
- `RK_IPV4_PARSE_MALFORMED` when present fields contradict the IPv4 format;
- `RK_IPV4_PARSE_FRAGMENTED` for a well-bounded fragment that this parser deliberately declines;
- `RK_IPV4_PARSE_OK` for one length-complete, unfragmented packet within this parser's scope.

Apply those checks in this order:

- reject a null input or output pointer as malformed;
- require the fixed 20-byte prefix;
- validate version and IHL;
- require the complete declared IHL;
- validate Total Length against IHL, then against `available`;
- reject More Fragments or a nonzero fragment offset.

The first failing check determines the result.
This makes a truncated declared boundary take precedence over a fragment flag that is already present.

Do not change the output view on failure.
Parse the two-byte fields from individual bytes; the input pointer need not be aligned.
Validate a larger value before subtracting a smaller one.

When `available` is at least 20, `packet` must address at least the fixed 20-byte IPv4 prefix.
It need not point to `available` contiguous bytes.
The parser does not read options or payload bytes.
It validates their declared boundaries so a later adapter can fetch those ranges through the storage-specific safe-copy interface.

Run:

```bash
./rootkitlings check
```

The checker compiles the exact parser for the pinned kernel and separately runs it with address and undefined-behavior sanitizers.
Its corpus covers every IHL byte, every 16-bit Total Length, every 16-bit fragment field, result precedence, an unaligned prefix, truncation, options, capture padding, and all protocol values.
It also gives the parser only 20 allocated bytes while declaring the maximum IHL and Total Length, so reading options or payload is detected.

This exercise does not validate the IPv4 checksum or interpret options, addresses, TTL, protocol-specific headers, or payload bytes.
It establishes only the boundaries that later packet code may safely use.

## References

- [RFC 791: IPv4 header format, IHL, Total Length, flags, and fragment offset](https://www.rfc-editor.org/rfc/rfc791.html#section-3.1)
- [RFC 1812: routers must not reject a packet solely for a nonzero reserved flag](https://www.rfc-editor.org/rfc/rfc1812.html#section-4.2.2.3)
- [Linux v7.2 `struct iphdr`](https://github.com/torvalds/linux/blob/v7.2/include/uapi/linux/ip.h)
- [Linux v7.2 `IP_CE` and fragment masks](https://github.com/torvalds/linux/blob/v7.2/include/net/ip.h#L142-L145)
- [Linux v7.2 fragment test](https://github.com/torvalds/linux/blob/v7.2/include/net/ip.h#L375-L378)
