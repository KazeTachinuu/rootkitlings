# L29: Authenticated encryption

Edit `crypto/aead.c` only.
Implement `rk_aead_seal_with()` and `rk_aead_open_with()`, then keep the two kernel entry points as thin calls to `rk_kernel_aead_ops`.

Run:

```sh
./rootkitlings check L29
```

## What this mechanism does

AES-256-GCM accepts a 32-byte secret key, a 12-byte nonce, plaintext, and associated data.
Sealing returns `ciphertext || 16-byte tag`.
Opening either authenticates and returns all plaintext or returns `-EBADMSG` and returns none.

Associated data is authenticated but remains visible.
It is the right place for clear fields whose exact bytes must be bound to the ciphertext, such as a version, message kind, or sequence number.
It is not extra plaintext.

For every seal operation, the nonce must be unique under that key.
GCM nonce reuse can expose relationships between plaintexts and destroy authentication.
This function is deliberately stateless: the caller, not this function, must allocate nonces without reuse.
Repeating a nonce while opening is allowed, but GCM does not detect replay.
A receiver still needs authenticated sequence state such as the mechanism from L28.

## Linux AEAD layout

The Linux 7.2 AEAD request sees one scatterlist in this lesson:

```text
seal:  AAD || plaintext || room for tag
open:  AAD || ciphertext || tag
```

`aead_request_set_ad()` gives the AAD length.
For seal, `cryptlen` is the plaintext length.
For open, `cryptlen` is the ciphertext-plus-tag length.
The same scatterlist is source and destination, so the provider works in place.
The public output excludes the AAD.

The transform must be configured for `gcm(aes)`, a 32-byte key, a 12-byte IV, and a 16-byte authentication tag.
Allocate the request after configuring the transform.
Keep the transform, request, scatterlist, IV, work buffer, and wait object alive until completion.

The provider may finish immediately or return `-EINPROGRESS` or `-EBUSY`.
Always pass the initial result to `wait_request`; the frozen Linux adapter maps that directly to `crypto_wait_req()`.
Use both `RK_AEAD_MAY_BACKLOG` and `RK_AEAD_MAY_SLEEP` because these calls run in sleepable process context.

## Transaction rule

Build and decrypt in the local work buffer.
Do not write the caller's output or length until the waited operation returns zero.
This is essential on `-EBADMSG`: a provider may have written unauthenticated candidate bytes before reporting the bad tag.
The same rule applies to every allocation, setup, and asynchronous failure.
Copying inputs into local storage before any output also makes overlapping input and output safe.

Each call owns its transform, request, wait object, IV, and work buffer, so calls do not share mutable state.
This helper has no start or stop state; nonce allocation and the lifetime of the surrounding module belong to its caller.

Free the request before the transform.
After input bytes have been copied locally, clear the complete work buffer and IV on every exit path.
Validation failures occur before either local buffer contains caller data.
Linux's `aead_request_free()` uses `kfree_sensitive()` for the request allocation, but no local cleanup can erase the caller-owned key or promise to erase undocumented copies held by a provider or device.

## Required behavior

- reject null pointers, AAD over 32 bytes, plaintext over 64 bytes, sealed
  input shorter than the tag or over 80 bytes, and short output capacity;
- preserve output bytes and output length on every failure;
- preserve `-EBADMSG` from authentication failure;
- reject unexpected IV or configured tag sizes;
- handle immediate, `-EINPROGRESS`, and `-EBUSY` completion;
- release each acquired object exactly once and, after copying caller data,
  clear both complete local buffers on every operation path.

The guest self-test checks `gcmEncryptExtIV256.rsp`, Count 0 in NIST's official vector set for the section with a 96-bit IV and 128-bit plaintext, AAD, and tag.
It also exercises every supported boundary around AES's 16-byte block size through the real Linux provider and changes every byte of the vector's ciphertext, tag, AAD, nonce, and key to verify rejection.
Host tests use an instrumented state machine to inspect request layout, deferred completion, object lifetime, errors, overlap, cleanup, and publication.
That fake is not a cryptographic oracle; only the NIST bytes judge AES-GCM output.

## Sources

- Linux v7.2 [`include/crypto/aead.h`](https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h)
- Linux v7.2 [`include/linux/crypto.h`](https://github.com/torvalds/linux/blob/v7.2/include/linux/crypto.h)
- [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final)
- [NIST CAVP GCM vector archive](https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/mac/gcmtestvectors.zip)
- [RFC 5116, AEAD interface and AES-256-GCM](https://www.rfc-editor.org/rfc/rfc5116.html)
