# Stage 5 architecture

Status: R18 is independently validated.

This document records only the accepted R18 contract.
It does not assign implementation status to later Stage 5 lessons.

## R18: IPv4 packet boundaries

Public alias: L17.
Stable ID: `hostile-packet-lengths`.
Title: Parse packet lengths before touching payload.
Direct prerequisites: none.
Required entry knowledge: `ipv4-packet-basics`.

The learner owns only `parser/ipv4.c`.
It implements `rk_parse_ipv4()`, an allocation-free parser that reports IPv4 header, packet, and payload boundaries without interpreting options or payload bytes.
The output names are `payload_offset` and `payload_length` because the IPv4 Protocol field may identify ICMP, GRE, IP-in-IP, or another protocol that is not a transport protocol.

`available` is the number of bytes the packet's backing storage can provide through its safe-copy interface.
When it is at least 20, the input pointer exposes at least the fixed 20-byte IPv4 prefix; it need not expose `available` contiguous bytes.
The parser reads only fixed-prefix fields.
It validates declared option and payload ranges so a storage-specific adapter can fetch them later.

The parser decodes Total Length and the fragment field byte by byte in network order, so the prefix may be unaligned.
IHL is converted from 32-bit words only after the version and minimum value are valid.
Total Length is compared with IHL and `available` before their difference is calculated.
Extra captured bytes after Total Length do not enlarge the payload view.

Validation order is part of the interface:

1. A null input or output pointer is malformed.
2. Fewer than 20 available bytes is truncated.
3. A version other than four or IHL below five is malformed.
4. An IHL beyond `available` is truncated.
5. Total Length below IHL is malformed.
6. Total Length beyond `available` is truncated.
7. More Fragments or a nonzero fragment offset is fragmented.

The first failing condition determines the result.
The output view is assembled locally and copied to the caller only after every check succeeds.
Every failure therefore leaves the caller's output unchanged.

RFC 791 reserves the high flag bit and requires senders to clear it.
RFC 1812 requires routers not to reject a packet solely because that bit is nonzero.
Linux v7.2 names it `IP_CE` and excludes it from `ip_is_fragment()`.
The receive-side parser follows the Linux policy: it ignores `IP_CE`, accepts Don't Fragment, and rejects only More Fragments or a nonzero offset.

The parser deliberately does not validate the checksum or interpret options, addresses, TTL, protocol-specific headers, or payload bytes.
Its success result means length-complete and unfragmented within this narrow contract, not fully valid for every later protocol use.

## Verification

The exact learner parser is compiled both into the pinned Linux v7.2 module and into a userspace fixture with address and undefined-behavior sanitizers.
The host corpus covers all 256 first octets, all 65,536 Total Length values, all 65,536 fragment fields, and all 256 Protocol values.
It also covers null pointers, every fixed-prefix truncation, options, capture padding, unaligned input, compound-error precedence, and unchanged output on failure.

A 20-byte heap allocation declares IHL 15 and Total Length 65,535.
That case proves the parser does not read options or payload through the fixed-prefix pointer.
Deliberately incorrect variants with reversed byte order, a fixed header length, accepted fragments, rejected `IP_CE`, early output mutation, or reads beyond the prefix must fail.

The kernel build uses `W=1` and accepts no warning.
The guest check loads and unloads the module, verifies both sysfs and `/proc/modules` lifecycle evidence, and requires a clean kernel log.

## Primary sources

- [RFC 791 section 3.1: IPv4 header format, IHL, Total Length, flags, and fragment offset](https://www.rfc-editor.org/rfc/rfc791.html#section-3.1)
- [RFC 1812 section 4.2.2.3: receive-side handling of unused IPv4 header bits](https://www.rfc-editor.org/rfc/rfc1812.html#section-4.2.2.3)
- [Linux v7.2 `struct iphdr`](https://github.com/torvalds/linux/blob/v7.2/include/uapi/linux/ip.h#L79-L101)
- [Linux v7.2 `IP_CE` and fragment masks](https://github.com/torvalds/linux/blob/v7.2/include/net/ip.h#L142-L145)
- [Linux v7.2 `ip_is_fragment()`](https://github.com/torvalds/linux/blob/v7.2/include/net/ip.h#L375-L378)
