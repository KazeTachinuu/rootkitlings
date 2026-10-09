# Stage 5 architecture

Status: R18 and R19 are independently validated.

This document records only accepted Stage 5 contracts.
It does not assign implementation status to later lessons.

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

## R19: bounded network-relative skb access

Public alias: L18.
Stable ID: `safe-skb-access`.
Title: Read packet bytes safely from an `sk_buff`.
Direct prerequisite: `hostile-packet-lengths`.

The learner owns only `access/skb_access.c` and implements `rk_skb_read_network()`.
The function copies 1 through 64 bytes from a nonnegative offset relative to the skb's network header and returns the logical byte count from that header to the end of the skb.
A supplied adapter reads the fixed 20-byte prefix required by R18.
Later classifiers can call the same primitive at a parser-proven payload offset instead of duplicating skb geometry.

Argument, geometry, and range validation are separate:

1. Reject null inputs or outputs, zero or over-limit lengths, and offsets beyond `S64_MAX`.
2. Reject `data_len > len`, `len > INT_MAX`, headroom beyond the signed helper range, and `INT_MIN` network offsets.
3. A negative network offset is valid only when its magnitude fits in headroom; logical availability is that magnitude plus `skb->len`.
4. A nonnegative network offset must not exceed `skb->len`; logical availability is the remaining length.
5. Reject an skb whose fragments are marked unreadable before reporting availability.
6. Reject a requested offset or length outside the logical network-relative range.
7. Add the base and relative offsets in a wide signed type, require the result to fit `int`, and guard the helper's internal signed `skb_headlen() - offset` subtraction against overflow.

The implementation calls `skb_header_pointer_careful()` with a fixed local 64-byte fallback.
It copies direct or gathered bytes into that local array first, then commits the requested bytes and `available` only after success.
Every failure leaves both outputs unchanged.
It never reads `skb->data` directly, allocates, pulls, linearizes, writes packet storage, or changes references.

The caller must hold a stable skb reference and prevent concurrent packet-data or geometry changes for the duration of the call.
The output regions are valid, writable, nonoverlapping, and separate from the skb and its storage.
The helper acquires no reference or lock and intentionally rejects lengths above 64 bytes.

The supplied kernel self-test covers every supported length, every 0-through-32 split between the linear head and page fragments for an IPv4-plus-UDP prefix, `frag_list`, shared and cloned storage, positive and negative network offsets, nonzero relative reads, every prefix truncation, malformed geometry, unreadable storage, transactional failures, packet and reference immutability, and R18 result mapping.
Independent audit added a transport-header read across every split and the signed-subtraction guard.
Compiling hostile variants prove that assuming offset zero, reporting raw `skb->len`, ignoring unreadable storage, changing any failure-output byte, limiting the generic API to the IPv4 prefix, using a non-careful helper, or reading `skb->data` directly is rejected.
The complete 330,001-assertion R18 sanitizer corpus and its guest lifecycle check run as regressions.

Primary implementation sources are Linux v7.2 `include/linux/skbuff.h` for skb geometry, `skb_network_offset()`, `skb_frags_readable()`, and `skb_header_pointer_careful()`, and `net/core/skbuff.c` for `skb_copy_bits()` across the linear head, page fragments, and `frag_list`.
