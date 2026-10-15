# Encrypt and authenticate with the real kernel AEAD API

The kernel crypto subsystem provides AES-256-GCM through the AEAD (Authenticated
Encryption with Associated Data) interface.
GCM gives you both confidentiality (encryption) and integrity (authentication)
in a single operation.
A tampered ciphertext fails decryption with -EBADMSG.

Uses the real kernel crypto API directly.
No wrappers, no fake headers.

## AEAD in one diagram

```text
   plaintext (N bytes)
        |
        v
  +-----------+
  | AES-GCM   |  key (32 bytes) + nonce (12 bytes)
  | encrypt   |  + optional AAD
  +-----------+
        |
        v
   ciphertext (N bytes) + tag (16 bytes)

   On decrypt: if ANY byte of ciphertext, tag, or AAD was
   modified, crypto_aead_decrypt returns -EBADMSG.
```

## Setup

```c
struct crypto_aead *tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
crypto_aead_setauthsize(tfm, 16);   /* 16-byte authentication tag */
crypto_aead_setkey(tfm, key, 32);   /* AES-256: 32-byte key */
```

## Scatterlist layout

The kernel AEAD API operates on scatterlists.
For a single contiguous buffer, use `sg_init_one`.
The buffer layout for encryption:

```text
+----------+-----------+-----------+
|   AAD    | plaintext | tag space |
+----------+-----------+-----------+
  aad_len     pt_len     16 bytes

sg_init_one(&sg, buf, aad_len + pt_len + 16)
```

The `aead_request_set_crypt` call takes the plaintext length (not including the
tag).
The kernel writes the tag into the 16 bytes after the plaintext.

For decryption, the same layout applies, but the tag is already present.
Pass the ciphertext length including the tag to `aead_request_set_crypt`.

## Encryption

```c
struct aead_request *req = aead_request_alloc(tfm, GFP_KERNEL);
aead_request_set_crypt(req, &sg, &sg, pt_len, nonce);
aead_request_set_ad(req, aad_len);
crypto_aead_encrypt(req);
```

After encryption, `buf + aad_len` contains `pt_len` bytes of ciphertext followed
by 16 bytes of authentication tag.

## Decryption

Same setup, but pass `ct_len` (which includes the 16-byte tag) to
`aead_request_set_crypt`, and call `crypto_aead_decrypt`.
Returns 0 on success, -EBADMSG if authentication fails.

## Your task

Edit `main.c`:

1. Implement `rk_aead_seal`: allocate a buffer for AAD + plaintext + tag, set up
   the scatterlist, encrypt, copy ciphertext + tag to the output.
2. Implement `rk_aead_open`: similar setup, call decrypt.
   Returns -EBADMSG on authentication failure.
3. Complete the self-test in `rk_init`: seal "hello", open it, verify the
   round-trip, then tamper with a byte and confirm -EBADMSG.

Run:

```sh
./rklings check
```

## References

- [`crypto_alloc_aead`](https://elixir.bootlin.com/linux/v7.2/source/include/crypto/aead.h)
- [`aead_request_set_crypt`](https://elixir.bootlin.com/linux/v7.2/source/include/crypto/aead.h)
- [`sg_init_one`](https://elixir.bootlin.com/linux/v7.2/source/include/linux/scatterlist.h)
- [Kernel crypto API docs](https://docs.kernel.org/7.2/crypto/api-aead.html)
