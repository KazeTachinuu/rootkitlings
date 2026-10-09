# Verify bytes with SHA-256

SHA-256 maps any byte string to a 32-byte digest.
Changing the input should change the digest, so a known digest can detect a changed artifact.

That statement depends on where the known digest came from.
If an attacker can replace both an artifact and the digest stored beside it, the comparison proves nothing about who supplied either file.
A SHA-256 digest is not sender authentication.
This exercise treats the expected digest as a separately trusted input; the next exercise adds a secret key with HMAC.

## Linux synchronous hash lifecycle

Use the supplied API in this order:

1. Allocate `"sha256"` with `crypto_alloc_shash()` and preserve an error-pointer result.
2. Require `crypto_shash_digestsize()` to be exactly 32 bytes.
3. Add `sizeof(struct shash_desc)` to `crypto_shash_descsize()` only after proving the addition cannot overflow.
4. Allocate that exact total and set `desc->tfm`.
5. Hash the complete buffer with `crypto_shash_digest()`.
6. Free the descriptor and transform on every path that owns them.

Preserve a negative Crypto API error.
Treat an impossible positive digest result as `-EPROTO`.

`crypto_shash_digest()` accepts an `unsigned int` length.
Reject a larger `size_t` before narrowing it.

Hash into a temporary 32-byte array.
Copy it to the caller only after every operation succeeds, so an error never publishes a partial digest.

Verification computes the digest the same way and compares all 32 bytes with the supplied `crypto_memneq()` operation.
Do not use `memcmp()` for digest verification because it may stop at the first differing byte.
Publish the Boolean result only after hashing succeeds.

## Your task

Edit one file:

- `crypto/sha256.c`

Implement:

- `rk_sha256_digest_with()`
- `rk_sha256_verify_with()`

The frozen adapter maps the supplied operations to the Linux v7.2 Crypto API.
The same learner file also runs against an injected host implementation so allocation sizes, cleanup, failures, and comparison work can be measured directly.

Run:

```bash
./rootkitlings check
```

The checker covers empty input, `"abc"`, binary inputs around the 64-byte SHA-256 block boundary, every input length through 513 bytes, every API and allocation failure, descriptor overflow boundaries, unchanged outputs, mismatch positions, an alternative implementation, hostile mutants, module-load rollback, reload, and clean guest logs.

## Scope

This exercise hashes one contiguous buffer with the synchronous API.
It does not stream a file, authenticate a sender, protect a secret, or decide how a trusted reference is distributed.

## References

- [Linux v7.2 synchronous message digest API](https://github.com/torvalds/linux/blob/v7.2/include/crypto/hash.h)
- [Linux v7.2 constant-time memory comparison](https://github.com/torvalds/linux/blob/v7.2/include/crypto/utils.h)
- [NIST FIPS 180-4: SHA-256 definition and test examples](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)
