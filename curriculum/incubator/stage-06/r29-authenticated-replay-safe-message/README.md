# Authenticate one fixed command and reject replay

This exercise accepts one harmless command: set a level from `0` through `3`.
It does not execute a shell command or expose a runtime control file.

Each message is exactly 36 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | version, exactly `1` |
| 1 | 1 | command, exactly `1` for `SET_LEVEL` |
| 2 | 8 | epoch, unsigned big-endian |
| 10 | 8 | sequence, unsigned big-endian |
| 18 | 1 | level, `0` through `3` |
| 19 | 1 | reserved, exactly `0` |
| 20 | 16 | first 16 bytes of HMAC-SHA-256 over bytes 0 through 19 |

The epoch, sequence, command, and argument are inside the authenticated body.
HMAC authenticates those bytes; it does not encrypt them.
Anyone who sees the message can read the level, epoch, and sequence.

Plain SHA-256 cannot authenticate a command because an attacker can calculate a new hash after changing the command.
HMAC-SHA-256 combines the message with a secret key.
Use the supplied `hmac(sha256)` context: the platform allocates the synchronous hash transform, installs the key with `crypto_shash_setkey()`, and calculates the MAC with `crypto_shash_digest()`.
Do not build a new keyed-hash construction from plain SHA-256.

## Your task

Edit one file:

- `message/authenticated.c`

Implement `rk_receiver_start()`, `rk_receiver_submit()`, and `rk_receiver_stop()`.

Initialize each receiver as `struct rk_receiver receiver = { 0 };` before its first start.
The caller must serialize `start` and `stop` against submissions; concurrent submissions are serialized by the supplied receiver lock.

The supplied platform layer owns the mutex and the Linux Crypto API transform.
It allocates `hmac(sha256)`, calls `crypto_shash_setkey()`, computes a digest with the synchronous hash API, compares tags with `crypto_memneq()`, and releases the transform.
`crypto_memneq()` avoids a comparison whose stopping point reveals the first different tag byte.

Your implementation must:

- require one 32-byte key, a nonzero epoch, and a nonzero first sequence;
- retain the crypto context, not the caller's key pointer;
- reject every length except 36 before reading the message;
- hold the supplied lock from HMAC calculation through the state update;
- compare exactly the fixed 16-byte tag;
- authenticate before interpreting version, command, epoch, sequence, or level;
- accept only the exact next sequence;
- reject a lower sequence as stale or duplicate;
- reject a higher sequence as a gap;
- accept `UINT64_MAX` once when it is expected, then mark the receiver exhausted;
- never wrap the expected sequence to zero;
- update the output, accepted count, and replay state only after every check succeeds;
- clear the calculated digest on every exit after it is created.

Holding one mutex through verification and commit is deliberate.
If concurrent callers submit the same valid sequence, exactly one advances the state and every other caller sees a duplicate.

## Return values

Return `0` only after committing a command.
Keep the receiver unchanged for every error.

- `rk_receiver_start()` returns `EINVAL` for invalid arguments, `EBUSY` for an
  already-started receiver, or the platform's allocation/key error.
- `rk_receiver_submit()` returns `EINVAL` for a null argument, `EMSGSIZE` for a
  non-36-byte message, and `ENODEV` for a receiver that is not started.
- A digest error is returned unchanged.
  A wrong tag returns `EBADMSG`.
- An authenticated bad version returns `EPROTONOSUPPORT`; a bad command returns
  `EOPNOTSUPP`; a bad level or reserved byte returns `EINVAL`.
- An authenticated wrong epoch returns `ESTALE`; a lower sequence returns
  `EALREADY`; a higher sequence returns `ERANGE`; an exhausted receiver returns
  `EOVERFLOW`.

Because authentication comes first, unauthenticated field changes return `EBADMSG`, not a grammar or replay error.

## Known answer

The checker fixes one protocol vector so byte order and truncation are unambiguous:

```text
key    000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
body   0101010203040506070800000000000000010300
HMAC   c73d5cc190dee29b65db6008af471d12d70989358f99c9e6fb49a466146d16f1
tag    c73d5cc190dee29b65db6008af471d12
```

The full HMAC is checked even though the wire format carries only its leftmost 16 bytes.

## Fixed tag budget

The protocol always uses the leftmost 128 bits of HMAC-SHA-256.
The length is fixed for the lifetime of the key and is not negotiated from the message.

For blind tag guesses, one attempt succeeds with probability at most `2^-128`.
Even after `2^32` failed attempts, the union bound is at most `2^-96`.
That budget assumes the 32-byte key remains secret and is generated correctly.
Rate limits and key rotation remain necessary operational controls.

FIPS 198-1 remains the final NIST HMAC standard while NIST proposes moving it to SP 800-224.
SP 800-224 is still an initial public draft and is cited as a draft below.
NIST SP 800-107 Revision 1 is final but planned for withdrawal after its remaining guidance is moved.

## Replay policy

This exercise uses a strict next-sequence policy, not an out-of-order window:

- the first accepted value is configured at startup;
- only that exact value is accepted;
- a smaller value is stale or duplicate;
- a larger value is a gap;
- after accepting `UINT64_MAX`, all later messages fail with `EOVERFLOW`.

RFC anti-replay windows allow controlled reordering.
The strict policy here is smaller and appropriate for the ordered command stream taught earlier.

The receiver keeps replay state only in memory.
Reloading with the same key, epoch, and first sequence accepts an old message again.
A real deployment must provision a fresh authenticated epoch or persist replay state across restart.
This lesson does neither and tests that limitation explicitly.

Run:

```bash
./rootkitlings check
```

The checker first validates its frozen HMAC adapter with RFC 4231 test case 1.
It also recomputes this protocol's known-answer tag with the independent OpenSSL command-line implementation before using the adapter to judge learner code.
It then covers caller-key erasure, wrong keys, every bit of every authenticated field and tag, every message length, authenticated grammar failures, stale, duplicate, gap, maximum, and wrap policy, crypto failures, restart behavior, and concurrent duplicate submissions.
Every rejected case must leave replay state and command output byte-for-byte unchanged.

## References

- [NIST FIPS 198-1, HMAC, final publication and proposed-withdrawal note](https://csrc.nist.gov/pubs/fips/198-1/final)
- [NIST SP 800-224, HMAC, initial public draft](https://csrc.nist.gov/pubs/sp/800/224/ipd)
- [NIST SP 800-107 Revision 1, final publication and planned-withdrawal note](https://csrc.nist.gov/pubs/sp/800/107/r1/final)
- [RFC 4302 anti-replay sequence and window rules](https://www.rfc-editor.org/rfc/rfc4302.html)
- [RFC 6479: update replay state only for an authenticated sequence](https://www.rfc-editor.org/rfc/rfc6479.html)
- [RFC 4231 HMAC-SHA-256 test vectors](https://www.rfc-editor.org/rfc/rfc4231.html)
- [Linux 7.2 synchronous hash API](https://github.com/torvalds/linux/blob/v7.2/include/crypto/hash.h)
- [Linux 7.2 constant-time `crypto_memneq()`](https://github.com/torvalds/linux/blob/v7.2/lib/crypto/memneq.c)
