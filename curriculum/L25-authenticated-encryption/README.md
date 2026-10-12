# Seal and open an encrypted envelope

Build a kernel helper for a small encrypted envelope.
Seal turns private bytes into ciphertext with an authentication tag.
Open releases the complete plaintext only when that tag is valid; a forged or
damaged envelope releases no plaintext to the caller.

## What this mechanism does

AES-256-GCM takes a 32-byte key, 12-byte nonce, plaintext, and associated data.
Seal returns `ciphertext || 16-byte tag`.
Open returns all authenticated plaintext or `-EBADMSG` and none.

Associated data remains visible but binds clear fields such as version, kind, or
sequence to the ciphertext.

Every seal nonce must be unique under its key; reuse can expose plaintext
relationships and destroy authentication.
This helper is stateless, so its caller allocates nonces and authenticated
replay state.
Repeated open nonces are valid cryptographically but do not prevent replay.

## Linux AEAD layout

A **scatterlist** describes a contiguous memory region for the crypto API.
The AEAD request sees one scatterlist in this lesson:

```text
seal:  AAD || plaintext || room for tag
open:  AAD || ciphertext || tag
```

`aead_request_set_ad()` gives the AAD length.
For seal, `cryptlen` is the plaintext length.
For open, `cryptlen` is the ciphertext-plus-tag length.
The same scatterlist is source and destination, so the provider works in place.
The public output excludes the AAD.

Configure `gcm(aes)` for a 32-byte key, 12-byte IV, and 16-byte tag, then
allocate the request.
Keep every request object and buffer alive until completion.

Always pass the result to `wait_request` (maps to `crypto_wait_req()`), which
handles immediate success, `-EINPROGRESS`, and `-EBUSY` completion.
Set `RK_AEAD_MAY_BACKLOG | RK_AEAD_MAY_SLEEP` (the operation may sleep and
queue behind other requests).

## Transaction rule

Build and decrypt locally.
Publish no caller output or length until the waited operation returns zero,
because a provider may write candidate plaintext before `-EBADMSG`.
This transaction rule covers setup, allocation, and asynchronous failures and
makes overlapping input and output safe.

Each call owns its mutable objects; module lifetime belongs to its caller.

Free the request before the transform.
After copying caller data locally, clear the complete work buffer and IV on
every exit.
Local cleanup cannot erase the caller's key or undocumented provider copies.

## Required behavior

- reject null pointers, AAD over 32 bytes, plaintext over 64 bytes, sealed input
  shorter than the tag or over 80 bytes, and short output capacity;
- preserve output bytes and output length on every failure;
- preserve `-EBADMSG` from authentication failure;
- reject unexpected IV or configured tag sizes;
- handle immediate, `-EINPROGRESS`, and `-EBUSY` completion;
- release each acquired object exactly once and, after copying caller data,
  clear both complete local buffers on every operation path.

The guest checks NIST `gcmEncryptExtIV256.rsp` Count 0 with the real provider,
plus block boundaries and tampering.
Instrumented host tests inspect layout, deferred completion, lifetime, overlap,
cleanup, and publication; only the NIST bytes judge cryptographic output.

## Your task

Edit `crypto/aead.c` only.
Implement `rk_aead_seal_with()` and `rk_aead_open_with()`, then keep the two
kernel entry points as thin calls to `rk_kernel_aead_ops`.

Run:

```sh
./rklings check
```

## References

- Linux v7.2 `include/crypto/aead.h`
  <https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h>
- Linux v7.2 `include/linux/crypto.h`
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/crypto.h>
- NIST SP 800-38D
  <https://csrc.nist.gov/pubs/sp/800/38/d/final>
- NIST CAVP GCM vector archive
  <https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/mac/gcmtestvectors.zip>
- RFC 5116, AEAD interface and AES-256-GCM
  <https://www.rfc-editor.org/rfc/rfc5116.html>
