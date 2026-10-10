# Reject deceptive IPv4 lengths

Before a traffic watcher inspects a signal, it must find the packet's real
boundaries.
An IPv4 header may claim bytes that are missing, overlap its own header, or
belong to fragments that this module cannot reassemble.

Build a bounded parser that accepts one complete, unfragmented packet and
classifies every rejection.
Success proves that later code can inspect the returned payload view without
trusting hostile length fields.

The module runs five representative parser cases when it loads.
A successful load publishes `parser_selftest=Y` in sysfs and records 14 kernel
assertions in the log.
The larger host corpus then sweeps the complete field ranges under
AddressSanitizer and UndefinedBehaviorSanitizer.

An IPv4 packet carries two different bounds:

- `available` is the number of bytes the packet's backing storage can provide
  through its safe-copy interface.
- The header's Total Length is the number of bytes the packet declares.

Validate both bounds before reporting the payload.

## The fields used here

The first byte holds version and IHL; IHL counts 32-bit words from 5 to 15.
Values below 5 are malformed IPv4 headers.

Big-endian Total Length at bytes 2 and 3 includes the header.
Less than IHL is malformed; more than `available` is truncated.
Bytes beyond Total Length do not enlarge the view.

The two-byte fragment field is also big-endian:

- Ignore the reserved top bit, following Linux v7.2 and RFC 1812 policy.
- `0x4000` is Don't Fragment.
  It does not mean that the packet is a fragment.
- `0x2000` is More Fragments.
- The low 13 bits are the fragment offset.

This parser rejects More Fragments and every nonzero fragment offset.
This lets later code treat the payload as a complete datagram; no reassembly is
performed.

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

- `RK_IPV4_PARSE_TRUNCATED` when the supplied bytes end before a required
  declared boundary;
- `RK_IPV4_PARSE_MALFORMED` when present fields contradict the IPv4 format;
- `RK_IPV4_PARSE_FRAGMENTED` for a well-bounded fragment that this parser
  deliberately declines;
- `RK_IPV4_PARSE_OK` for one length-complete, unfragmented packet within this
  parser's scope.

Apply those checks in this order:

- reject a null input or output pointer as malformed;
- require the fixed 20-byte prefix;
- validate version and IHL;
- require the complete declared IHL;
- validate Total Length against IHL, then against `available`;
- reject More Fragments or a nonzero fragment offset.

The first failure wins, so truncated declared bounds precede fragmentation.

Do not change the output view on failure.
Parse the two-byte fields from individual bytes; the input pointer need not be
aligned.
Validate a larger value before subtracting a smaller one.

When `available >= 20`, `packet` addresses only the fixed 20-byte prefix.
It need not expose `available` contiguous bytes.
Do not read options or payload; only validate their declared bounds.

Run:

```sh
./rootkitlings check
```

The kernel check covers a valid packet, truncation, malformed input, fragment
policy, output preservation, and Linux's reserved-flag policy.
Sanitized host checks cover every IHL, Total Length, fragment field, precedence,
unaligned input, options, padding, and protocol value.
A guarded 20-byte allocation detects reads into declared options or payload.

Checksums, options, addresses, TTL, transport headers, and payload are out of
scope.

## References

- [RFC 791: IPv4 header format, IHL, Total Length, flags, and fragment
  offset][1]
- [RFC 1812: routers must not reject a packet solely for a nonzero reserved
  flag][2]
- [Linux v7.2 `struct iphdr`][3]
- [Linux v7.2 `IP_CE` and fragment masks][4]
- [Linux v7.2 fragment test][5]

[1]: https://www.rfc-editor.org/rfc/rfc791.html#section-3.1
[2]: https://www.rfc-editor.org/rfc/rfc1812.html#section-4.2.2.3
[3]: https://github.com/torvalds/linux/blob/v7.2/include/uapi/linux/ip.h
[4]: https://github.com/torvalds/linux/blob/v7.2/include/net/ip.h#L142-L145
[5]: https://github.com/torvalds/linux/blob/v7.2/include/net/ip.h#L375-L378
