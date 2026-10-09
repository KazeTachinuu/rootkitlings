# Encode and decode a bounded DNS wire name

DNS encodes length-prefixed labels followed by a zero-length root label.
For example, `a.example` is:

```text
01 61 07 65 78 61 6d 70 6c 65 00
```

A wire name is at most 255 bytes, and each ordinary label is 1 to 63 bytes.
Label data is binary, so embedded zero and dot bytes are data.

## Your task

Edit one file:

- `codec/dns_name.c`

Implement both functions declared in `include/dns_name.h`:

- `rk_dns_name_encode()` writes one complete uncompressed name.
- `rk_dns_name_parse()` describes one complete uncompressed name already in a
  bounded buffer.

Neither function allocates; every failure leaves outputs and destination bytes
unchanged.

## Encoding

Validate all input before capacity.
Build a local 255-byte candidate, then copy once to support overlapping labels.

Use this result order:

- `RK_DNS_NAME_INVALID_ARGUMENT` for a missing required pointer, including
  missing label data.
  This check precedes that label's length check.
- `RK_DNS_NAME_MALFORMED` for an empty label, a label longer than 63 bytes, more
  than 127 labels, or a complete name longer than 255 bytes.
- `RK_DNS_NAME_NO_SPACE` when the valid complete name does not fit in
  `capacity`.
- `RK_DNS_NAME_OK` after committing the complete name and `used` length.

Zero input labels encodes the root name as one zero byte.
The `used` output object must not overlap the `wire` output region or label
storage.

## Parsing

Parse within `available` and commit only after finding the root.
Record each label's data offset and length plus bytes consumed through root;
later bytes remain unconsumed.

At each label boundary, use this order:

- Reaching the 255-byte limit without a root is `RK_DNS_NAME_MALFORMED`.
- Reaching the end of the available bytes while a root could still fit is
  `RK_DNS_NAME_TRUNCATED`.
- A zero byte completes the name.
- A length byte whose high bits are `11` is
  `RK_DNS_NAME_UNSUPPORTED_COMPRESSION`.
- High bits `01` or `10` are `RK_DNS_NAME_MALFORMED`.
- For a length from 1 through 63, first check whether that label and a later
  root can fit in the 255-byte name limit.
  If not, the name is malformed.
  If they can fit but the supplied bytes end inside the label, the input is
  truncated.

Report compression but do not follow it; safe traversal needs message context,
pointer rules, and a budget absent from this API.

Run:

```bash
./rootkitlings check
```

Sanitized checks cover every length class, maximum names, truncation, capacity,
binary and unaligned input, overlap, precedence, and compression markers.
The same C file also builds with `W=1` and runs kernel self-tests.

## Scope

This codec handles only an uncompressed name component.
It does not parse messages, follow pointers, validate hostnames, compare names,
or resolve them.

## References

- [RFC 1035: label encoding and the 63-byte and 255-byte limits][1]
- [RFC 1035: compression and the requirement for general implementations to
  understand it][2]
- [RFC 2181: every binary label value is valid in DNS][3]
- [RFC 8499: the root label occupies one wire byte][4]
- [RFC 9267: bounds and compression-pointer safety failures][5]

[1]: https://www.rfc-editor.org/rfc/rfc1035.html#section-3.1
[2]: https://www.rfc-editor.org/rfc/rfc1035.html#section-4.1.4
[3]: https://www.rfc-editor.org/rfc/rfc2181.html#section-11
[4]: https://www.rfc-editor.org/rfc/rfc8499.html#section-3
[5]: https://www.rfc-editor.org/rfc/rfc9267.html#section-2
