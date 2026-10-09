# Authenticate one fixed command and reject replay

This exercise authenticates one command: set a level from `0` through `3`.
It neither encrypts the fields nor exposes a runtime control file.

Each message is exactly 36 bytes:

| Offset | Size | Field                                                  |
| -----: | ---: | ------------------------------------------------------ |
|      0 |    1 | version, exactly `1`                                   |
|      1 |    1 | command, exactly `1` for `SET_LEVEL`                   |
|      2 |    8 | epoch, unsigned big-endian                             |
|     10 |    8 | sequence, unsigned big-endian                          |
|     18 |    1 | level, `0` through `3`                                 |
|     19 |    1 | reserved, exactly `0`                                  |
|     20 |   16 | first 16 bytes of HMAC-SHA-256 over bytes 0 through 19 |

Plain SHA-256 cannot authenticate a command because an attacker can hash a
changed command.
Use the supplied `hmac(sha256)` context, which installs the key with
`crypto_shash_setkey()` and computes the MAC with `crypto_shash_digest()`.
Do not invent a keyed construction from plain SHA-256.

## Your task

Edit `message/authenticated.c`.
Implement `rk_receiver_start()`, `rk_receiver_submit()`, and
`rk_receiver_stop()`.
Initialize each receiver as `struct rk_receiver receiver = { 0 };` before its
first start.
The caller serializes `start` and `stop` against submissions; the supplied lock
serializes concurrent submissions.

Start requires one 32-byte key, a nonzero epoch, and a nonzero first sequence.
Retain the crypto context, never the caller's key pointer.
The platform owns the mutex and transform, compares tags with `crypto_memneq()`,
and releases the transform.

Submit must:

- reject every length except 36 before reading the message;
- hold the lock from HMAC calculation through commit;
- compare exactly the fixed 16-byte tag;
- authenticate before interpreting any field;
- accept only the exact next sequence;
- accept `UINT64_MAX` once, then mark the receiver exhausted;
- never wrap the expected sequence to zero;
- commit output, count, and replay state only after every check passes;
- clear the calculated digest on every exit after creating it.

Exactly one of concurrent identical valid submissions may commit.
Every rejection leaves receiver state and command output unchanged.

```text
unstarted -- start(epoch, sequence) --> ready(expected=sequence)
ready -- valid exact sequence -------> ready(expected=sequence + 1)
ready -- valid UINT64_MAX -----------> exhausted
ready -- any rejection --------------> ready(state unchanged)
ready or exhausted -- stop ----------> unstarted
```

## Errors and replay policy

`rk_receiver_start()` returns `EINVAL` for invalid arguments, `EBUSY` if
started, or the platform allocation/key error.
`rk_receiver_submit()` returns `EINVAL` for null arguments, `EMSGSIZE` for the
wrong length, and `ENODEV` before start.
Return digest errors unchanged and use `EBADMSG` for a wrong tag.
After authentication, use `EPROTONOSUPPORT` for version, `EOPNOTSUPP` for
command, `EINVAL` for level or reserved byte, `ESTALE` for epoch, `EALREADY` for
a lower sequence, `ERANGE` for a higher sequence, and `EOVERFLOW` after
exhaustion.
Unauthenticated field changes therefore return `EBADMSG`.

This is strict ordered replay protection, not an out-of-order window.
Replay state is only in memory, so reloading with the same key, epoch, and first
sequence accepts old traffic.
A deployment must use a fresh authenticated epoch or persist replay state.

## Known answer and tag budget

```text
key    000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
body   0101010203040506070800000000000000010300
HMAC   c73d5cc190dee29b65db6008af471d12d70989358f99c9e6fb49a466146d16f1
tag    c73d5cc190dee29b65db6008af471d12
```

The tag is the fixed leftmost 128 bits of HMAC-SHA-256.
A blind guess succeeds with probability at most `2^-128`; after `2^32` attempts,
the union bound is `2^-96`.
This requires a secret key, rate limits, and key rotation.

Run:

```bash
./rootkitlings check
```

The checker validates the adapter with RFC 4231, recomputes the known answer
with OpenSSL, and tests authentication, replay, concurrency, failures, cleanup,
and unchanged outputs.

## References

- NIST FIPS 198-1, HMAC, final publication and proposed-withdrawal note
  <https://csrc.nist.gov/pubs/fips/198-1/final>
- NIST SP 800-224, HMAC, initial public draft
  <https://csrc.nist.gov/pubs/sp/800/224/ipd>
- NIST SP 800-107 Revision 1, final publication and planned-withdrawal note
  <https://csrc.nist.gov/pubs/sp/800/107/r1/final>
- RFC 4302 anti-replay sequence and window rules
  <https://www.rfc-editor.org/rfc/rfc4302.html>
- RFC 6479: update replay state only for an authenticated sequence
  <https://www.rfc-editor.org/rfc/rfc6479.html>
- RFC 4231 HMAC-SHA-256 test vectors
  <https://www.rfc-editor.org/rfc/rfc4231.html>
- Linux 7.2 synchronous hash API
  <https://github.com/torvalds/linux/blob/v7.2/include/crypto/hash.h>
- Linux 7.2 constant-time `crypto_memneq()`
  <https://github.com/torvalds/linux/blob/v7.2/lib/crypto/memneq.c>
