# Choose the protection before the algorithm

An encoding changes how bytes are represented.
It does not stop another party from reading or changing them.

XOR makes that distinction visible.
Applying the same repeating key twice restores the original bytes:

```text
plaintext XOR key = encoded bytes
encoded bytes XOR key = plaintext
```

This can remove an obvious string from a module file, but the key and the encoded bytes are stored together.
Known plaintext reveals the corresponding key bytes, reused key bytes reveal other messages, and flipping one encoded bit flips the same plaintext bit without being detected.
That is obfuscation, not confidentiality or integrity.

## Choose from the required property

For a small authenticated control message, use the smallest plan that provides the required properties:

| Required property | Suitable plan | Condition that must already be true |
|---|---|---|
| Representation only | Encoding | Both sides agree on the format |
| Detect a changed artifact | SHA-256 | The expected digest comes from a trusted source |
| Integrity and sender authentication | HMAC-SHA-256 | Both sides own the shared key |
| Authentication and freshness | HMAC-SHA-256 plus a sequence number | The receiver remembers accepted sequence state |
| Confidentiality, integrity, and authentication | AEAD | The key is secret and the nonce is unique for that key |
| Confidentiality and freshness | AEAD plus replay state | The nonce is unique and the receiver rejects old messages |

A digest stored beside an artifact is not a trusted reference when an attacker can replace both.
HMAC does not hide the message.
It proves that a holder of the shared key authenticated the message; it cannot distinguish between several parties that share that key.
A sequence number prevents replay only when it is authenticated and the receiver remembers what it has accepted, including across any reset where old messages must remain invalid.
AEAD authenticates its plaintext and any associated data, but a protocol still needs replay state.

Payload protection also leaves traffic evidence.
Observers can still see endpoints, ports, timing, packet sizes, and message frequency unless a separate design addresses them.

## Your task

Edit one file:

- `protection/properties.c`

Implement:

- `rk_xor_repeat()` as a bounded reversible transformation.
- `rk_choose_protection()` as the decision table above.

The XOR function must validate the whole request before changing the destination.
It must accept every byte value and remain correct when the input, key, and destination overlap.
All three pointers are required, including for an empty input.
Use this result order:

- `RK_XOR_INVALID_ARGUMENT` for a missing pointer or empty key.
- `RK_XOR_TOO_LONG` when the input or key exceeds its declared maximum.
- `RK_XOR_NO_SPACE` when an otherwise valid result does not fit.
- `RK_XOR_OK` only after committing the complete result.

The selector receives required-property bits and available-condition bits.
Confidentiality always selects authenticated encryption, never XOR or encryption without authentication.
Freshness always selects an authenticated plan with replay state.
For integrity alone, select a trusted SHA-256 reference when one is available; otherwise HMAC is valid when a shared key is available.
Return `RK_PROTECTION_UNAVAILABLE` when the required conditions are absent or either input contains an unknown bit.

Run:

```bash
./rootkitlings check
```

The checker exercises every property and condition combination.
It also recovers a reused XOR key from known plaintext, decrypts a second message, demonstrates an undetected bit flip, proves the demonstration plaintext is absent from the module file but present while the module runs, builds with `W=1`, and runs the self-test in the disposable guest.

## Scope

This exercise does not implement SHA-256, HMAC, AEAD, key generation, or a wire protocol.
The following exercises implement and test those mechanisms after their required properties are clear.
XOR remains only a measurable obfuscation example.

## References

- [NIST FIPS 180-4: secure hash algorithms produce message digests](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)
- [NIST FIPS 198-1: HMAC uses a shared secret key for message authentication](https://csrc.nist.gov/pubs/fips/198-1/final)
- [NIST SP 800-38D: GCM authenticated encryption](https://csrc.nist.gov/pubs/sp/800/38/d/final)
- [RFC 5116: AEAD inputs, associated data, and nonce requirements](https://www.rfc-editor.org/rfc/rfc5116.html)
