# Choose protection for each threat

Build the decision point for a small secure-message toolkit.
Given the required protection and the keys or trusted references available, it
must choose a suitable mechanism or refuse the request.

The first test is repeating-key XOR.
It changes how bytes look, but not who can read or alter them:

```text
plaintext XOR key = encoded bytes
encoded bytes XOR key = plaintext
```

Storing the key beside the encoded bytes only hides an obvious string.
Known plaintext reveals reused key bytes, which expose other messages, and an
attacker can flip plaintext bits without detection.
That is obfuscation, not confidentiality or integrity.

## Choose from the required property

- **Representation only:** use an agreed encoding.
- **Changed artifact:** use SHA-256 with a separately trusted digest.
- **Message integrity and origin:** use HMAC-SHA-256 with a shared key.
- **Authentication and freshness:** authenticate a sequence number and retain
  replay state.
- **Confidentiality and authentication:** use AEAD with a secret key and a nonce
  unique for that key.
- **Confidentiality and freshness:** combine AEAD with replay state.

A digest stored beside an artifact is not trusted if an attacker can replace
both.
HMAC proves that some shared-key holder authenticated visible bytes; it does not
identify which holder.
AEAD authenticates plaintext and associated data, but does not supply replay
state.
Replay protection must survive any reset after which old messages remain
invalid.
Payload protection also leaves endpoints, ports, timing, sizes, and frequency
visible.

## Your task

Edit `protection/properties.c`.
Implement `rk_xor_repeat()` and `rk_choose_protection()`.

Validate the complete XOR request before changing the destination.
Accept every byte value and overlapping input, key, and destination.
Require all three pointers even for empty input.
Return results in this order:

1. `RK_XOR_INVALID_ARGUMENT` for a missing pointer or empty key;
2. `RK_XOR_TOO_LONG` for input or key beyond its declared maximum;
3. `RK_XOR_NO_SPACE` when an otherwise valid result does not fit;
4. `RK_XOR_OK` only after committing the complete result.

The selector receives required-property and available-condition bits.
Confidentiality always selects authenticated encryption, never XOR or
unauthenticated encryption.
Freshness always selects authentication with replay state.
For integrity alone, prefer a trusted SHA-256 reference; otherwise HMAC is valid
when a shared key exists.
Return `RK_PROTECTION_UNAVAILABLE` when conditions are missing or either input
contains an unknown bit.

Run:

```sh
./rootkitlings check
```

The checker covers every selection, overlap and boundary behavior, XOR key
recovery and bit flipping, module-string evidence, `W=1`, and the guest
self-test.

This lesson does not implement SHA-256, HMAC, AEAD, key generation, or a wire
protocol.

## References

- NIST FIPS 180-4: secure hash algorithms produce message digests
  <https://csrc.nist.gov/pubs/fips/180-4/upd1/final>
- NIST FIPS 198-1: HMAC uses a shared secret key for message authentication
  <https://csrc.nist.gov/pubs/fips/198-1/final>
- NIST SP 800-38D: GCM authenticated encryption
  <https://csrc.nist.gov/pubs/sp/800/38/d/final>
- RFC 5116: AEAD inputs, associated data, and nonce requirements
  <https://www.rfc-editor.org/rfc/rfc5116.html>
