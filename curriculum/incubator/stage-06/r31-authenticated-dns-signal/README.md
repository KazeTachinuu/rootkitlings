# L30: Emit an authenticated DNS-shaped signal

This exercise sends one status datagram to `127.0.0.1:5300`.
It uses the DNS question layout so a packet capture can decode it with ordinary DNS fields, but it does not ask a resolver to look up a name.
It never leaves the guest and it is not a command channel.

The status is one number from `0` through `3`.
An epoch and sequence number make each accepted status fresh.
HMAC-SHA-256 authenticates those fields with the laboratory key.
The first 16 HMAC bytes travel in the packet.

## Packet layout

The packet is one 116-byte TXT/IN question:

```text
DNS header
03 "rk1"
28 40 lowercase hex characters: version, kind, epoch, sequence, status
20 32 lowercase hex characters: 16-byte HMAC tag
0c "rootkitlings"
07 "example"
00
00 10  QTYPE=TXT
00 01  QCLASS=IN
```

The authenticated body is the fixed 20-byte body from L28:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | version, exactly `1` |
| 1 | 1 | kind, exactly `1` for a status signal |
| 2 | 8 | epoch, unsigned big-endian |
| 10 | 8 | sequence, unsigned big-endian |
| 18 | 1 | status, `0` through `3` |
| 19 | 1 | reserved, exactly `0` |

The transaction ID is DNS routing metadata and is not part of that body.
A forwarding resolver may replace it without changing the authenticated status.

Hex encoding makes the binary message fit DNS labels.
It adds no secrecy.
Anyone who captures the packet can read the epoch, sequence, and status.
The key is embedded for this lab and can be recovered from the module.

## Your task

Edit one file:

- `signal/authenticated_dns.c`

Implement:

- `rk_dns_signal_build()`;
- `rk_dns_signal_receive()`.

`rk_dns_signal_build()` must:

- reject a missing pointer, a key whose size is not 32 bytes, epoch zero,
  sequence zero, or a status above `3`;
- reject a destination shorter than 116 bytes;
- encode the fixed body in private storage;
- calculate HMAC-SHA-256 over exactly those 20 bytes;
- append exactly the first 16 digest bytes;
- call the supplied DNS encoder;
- publish the packet and used length only after every step succeeds;
- clear the private packet, message, and digest on every exit after use.

`rk_dns_signal_receive()` must:

- decode into a private 36-byte message;
- pass only a complete decoded message to the supplied replay receiver;
- clear the private message before returning.

The receiver authenticates before reading the body fields.
It accepts only its exact next sequence and advances replay state only after every check passes.
A duplicate, an old epoch, a sequence gap, a wrong key, a changed byte, or a truncated packet must leave that state unchanged.

The supplied UDP transport creates one kernel socket, sends the complete packet, and releases the socket before returning.
No socket remains for module exit to clean up.

## Verify

Run:

```sh
./rootkitlings check L30
```

The host checker uses OpenSSL as an independent known-answer oracle, tests every short output capacity and packet length, changes every protected or structural packet byte, and checks wrong-key, stale, duplicate, and gap behavior.
It also runs a second correct implementation and rejects behavioral mutants.

The guest checker starts a UDP listener before loading the module.
It decodes the captured datagram and calculates the HMAC with `sha256sum`; it does not trust a status value printed by the module.

## What a defender can see

Authentication does not conceal this traffic.
A capture exposes a TXT question with fixed `rk1.rootkitlings.example` labels, two long hexadecimal labels, visible counters, and a stable 116-byte size.
Regular timing, repeated `.example` questions, unusual hexadecimal labels, and sequence gaps are direct detection signals.
A defender who has the key can verify tags and reject captured duplicates with the same replay rule.

Because this lesson sends directly to a loopback UDP port, it is only a local protocol demonstration.
Ordinary resolver logs will not record it; packet or socket observation inside the guest will.

## References

- [RFC 1035, DNS message and label format](https://www.rfc-editor.org/rfc/rfc1035.html)
- [RFC 2104, HMAC](https://www.rfc-editor.org/rfc/rfc2104.html)
- [RFC 4302, authenticated sequence numbers and replay checking](https://www.rfc-editor.org/rfc/rfc4302.html)
- [Linux networking API, `kernel_sendmsg()`](https://docs.kernel.org/networking/kapi.html)
- [Linux 7.2 synchronous hash API](https://github.com/torvalds/linux/blob/v7.2/include/crypto/hash.h)
