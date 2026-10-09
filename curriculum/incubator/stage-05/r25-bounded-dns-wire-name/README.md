# Encode and decode a bounded DNS wire name

DNS sends a name as labels, not as a dot-separated string.
Each label starts with one length byte and the name ends with a zero-length root label.
For example, `a.example` is:

```text
01 61 07 65 78 61 6d 70 6c 65 00
```

The complete wire name is at most 255 bytes, including every length byte and the final zero.
An ordinary label is 1 to 63 bytes.
Label data is binary: a zero byte or a dot byte inside a label is data, not a separator.
That is why this exercise uses byte spans instead of a dotted C string.

## Your task

Edit one file:

- `codec/dns_name.c`

Implement both functions declared in `include/dns_name.h`:

- `rk_dns_name_encode()` writes one complete uncompressed name.
- `rk_dns_name_parse()` describes one complete uncompressed name already in a bounded buffer.

Neither function allocates memory.
On every failure, it must leave all output values and destination bytes unchanged.

## Encoding

Validate the entire input name before checking destination capacity.
Build the name in a local 255-byte candidate, then copy the complete result to `wire` once.
This also makes encoding work when label data overlaps the destination buffer.

Use this result order:

- `RK_DNS_NAME_INVALID_ARGUMENT` for a missing required pointer, including missing label data.
  This check precedes that label's length check.
- `RK_DNS_NAME_MALFORMED` for an empty label, a label longer than 63 bytes, more than 127 labels, or a complete name longer than 255 bytes.
- `RK_DNS_NAME_NO_SPACE` when the valid complete name does not fit in `capacity`.
- `RK_DNS_NAME_OK` after committing the complete name and `used` length.

Zero input labels encodes the root name as one zero byte.
The `used` output object must not overlap the `wire` output region or label storage.

## Parsing

Parse only within `available` and commit a local view only after finding the root label.
The view records each label's data offset and length, plus the bytes consumed through the root.
Bytes after that root remain unconsumed.

At each label boundary, use this order:

- Reaching the 255-byte limit without a root is `RK_DNS_NAME_MALFORMED`.
- Reaching the end of the available bytes while a root could still fit is `RK_DNS_NAME_TRUNCATED`.
- A zero byte completes the name.
- A length byte whose high bits are `11` is `RK_DNS_NAME_UNSUPPORTED_COMPRESSION`.
- High bits `01` or `10` are `RK_DNS_NAME_MALFORMED`.
- For a length from 1 through 63, first check whether that label and a later root can fit in the 255-byte name limit.
  If not, the name is malformed.
  If they can fit but the supplied bytes end inside the label, the input is truncated.

Compression is reported separately but not followed.
A safe compression parser needs the complete DNS message, the name's offset in that message, pointer-target rules, and a traversal budget.
Those inputs do not exist in this API.

Run:

```bash
./rootkitlings check
```

The checker uses address and undefined-behavior sanitizers.
It covers every first length byte, every ordinary label length, both maximum-size name shapes, every truncation and output capacity, binary label data, unaligned input, overlap, failure precedence, reserved label types, and compression markers at valid label boundaries.
It also builds the module with `W=1`, runs its kernel self-test in the disposable guest, and reruns the accepted IPv4-boundary lesson.

## Scope

This codec produces the uncompressed name component used by a later DNS-shaped signal.
RFC 1035 permits a sender to omit compression, but a general DNS implementation must understand compressed names where the format allows them.
This exercise does not parse a DNS message, follow compression pointers, validate hostnames, perform case-insensitive comparison, or implement resolver behavior.
Binary labels allowed by DNS may still be unsuitable for a particular application.

## References

- [RFC 1035: label encoding and the 63-byte and 255-byte limits](https://www.rfc-editor.org/rfc/rfc1035.html#section-3.1)
- [RFC 1035: compression and the requirement for general implementations to understand it](https://www.rfc-editor.org/rfc/rfc1035.html#section-4.1.4)
- [RFC 2181: every binary label value is valid in DNS](https://www.rfc-editor.org/rfc/rfc2181.html#section-11)
- [RFC 8499: the root label occupies one wire byte](https://www.rfc-editor.org/rfc/rfc8499.html#section-3)
- [RFC 9267: bounds and compression-pointer safety failures](https://www.rfc-editor.org/rfc/rfc9267.html#section-2)
