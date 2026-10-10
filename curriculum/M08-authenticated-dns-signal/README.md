# Send an authenticated DNS-shaped status flare

Build a one-shot status flare for a resident module.
It packs status `0` through `3`, an epoch, a sequence, and an HMAC tag into a
DNS question sent to `127.0.0.1:5300`.
Ordinary capture tools can decode it, but only a receiver with the shared key
can authenticate it.

The flare performs no lookup, never leaves the guest, and is not a command
channel.

The receiver authenticates the epoch and accepts only the next sequence, so a
captured status cannot be replayed within that receiver state.
The packet carries the first 16 HMAC-SHA-256 bytes under the supplied test key.

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

The authenticated body is the fixed 20-byte body from the authenticated
replay-safe message exercise:

| Offset | Size | Field                                 |
| -----: | ---: | ------------------------------------- |
|      0 |    1 | version, exactly `1`                  |
|      1 |    1 | kind, exactly `1` for a status signal |
|      2 |    8 | epoch, unsigned big-endian            |
|     10 |    8 | sequence, unsigned big-endian         |
|     18 |    1 | status, `0` through `3`               |
|     19 |    1 | reserved, exactly `0`                 |

The unauthenticated transaction ID is routing metadata that a resolver may
replace without changing the status.

Hex makes binary data fit DNS labels but adds no secrecy.
The epoch, sequence, and status are visible, and the lab key is recoverable from
the module.

## Your task

Edit one file:

- `signal/authenticated_dns.c`

Implement:

- `rk_dns_signal_build()`;
- `rk_dns_signal_receive()`.

Build must:

- reject a missing pointer, a key whose size is not 32 bytes, epoch zero,
  sequence zero, or a status above `3`;
- reject a destination shorter than 116 bytes;
- encode the fixed body in private storage;
- calculate HMAC-SHA-256 over exactly those 20 bytes;
- append exactly the first 16 digest bytes;
- call the supplied DNS encoder;
- publish the packet and used length only after every step succeeds;
- clear the private packet, message, and digest on every exit after use.

Receive must:

- decode into a private 36-byte message;
- pass only a complete decoded message to the supplied replay receiver;
- clear the private message before returning.

Authenticate before reading fields, accept only the exact next sequence, and
advance replay state only after every check passes.
Duplicate, stale, gap, wrong-key, changed, and truncated inputs leave state
unchanged.

The supplied UDP transport creates one socket, sends the complete packet, and
releases it before returning, leaving no exit-time socket.

## Check

Run:

```sh
./rootkitlings check
```

The host uses OpenSSL as an independent oracle and checks boundaries, packet
structure, authentication, replay, unchanged state, and behavioral mutants.

The guest captures the datagram and recomputes its HMAC with `sha256sum` rather
than trusting module output.

## What a defender can see

Authentication does not conceal the fixed `rk1.rootkitlings.example` labels,
hexadecimal data, counters, 116-byte size, or timing.
These features and sequence gaps are detection signals.
A defender with the key can verify tags and apply the same replay rule.

This direct loopback demonstration bypasses ordinary resolver logs but remains
visible to packet or socket observation inside the guest.

## References

- RFC 1035, DNS message and label format
  <https://www.rfc-editor.org/rfc/rfc1035.html>
- RFC 2104, HMAC
  <https://www.rfc-editor.org/rfc/rfc2104.html>
- RFC 4302, authenticated sequence numbers and replay checking
  <https://www.rfc-editor.org/rfc/rfc4302.html>
- Linux networking API, `kernel_sendmsg()`
  <https://docs.kernel.org/networking/kapi.html>
- Linux 7.2 synchronous hash API
  <https://github.com/torvalds/linux/blob/v7.2/include/crypto/hash.h>
