# 50 - Encrypt and authenticate a message

Encryption hides a message, but encryption alone does not prove that the ciphertext was left unchanged.
An attacker who modifies encrypted bytes may cause modified plaintext after decryption.

AES-GCM produces two outputs together:

```text
plaintext + key + nonce
          |
          v
       AES-GCM
          |
          +----> ciphertext   hides the message
          |
          +----> tag          detects modification
```

Decryption releases the plaintext only when the tag is valid.
A changed ciphertext, tag, nonce, or key produces `-EBADMSG`.

## Known input and output

The module uses a published AES-GCM test vector with a 16-byte zero key, 12-byte zero nonce, and 16-byte zero plaintext.
That fixed input has one exact result:

```text
plaintext   00000000000000000000000000000000
ciphertext  0388dace60b6a392f328c2b971b2fe78
tag         ab6e47d42cec13bdf53a67b21257bddf
```

Comparing the bytes with a known answer proves that the algorithm was configured correctly.
Decrypting them proves the round-trip.
Flipping one ciphertext bit proves authentication failure.

The fixed nonce exists only to reproduce this test vector.
A real key must never encrypt two different messages with the same nonce.

## Kernel request

The existing code allocates `gcm(aes)`, installs the key, selects a 16-byte tag, allocates a request, and describes the in-place buffer with a scatterlist.

```text
work buffer
    |
    v
scatterlist ----> aead_request
                       |
             +---------+---------+
             |                   |
          encrypt              decrypt
             |                   |
             +---- crypto_wait --+
                       |
                       v
              result or -EBADMSG
```

Crypto implementations may finish immediately or asynchronously.
`crypto_wait_req()` handles both cases and returns the final status.

## Submit the request

```sh
./rootkitlings start L50
```

Complete `submit_request()` in this order:

1. Use `aead_request_set_callback()` with `CRYPTO_TFM_REQ_MAY_BACKLOG`, `crypto_req_done`, and the local wait object.
2. Set the associated-data length to zero with `aead_request_set_ad()`.
3. Attach the in-place scatterlist, byte count, and nonce with `aead_request_set_crypt()`.
4. Call `crypto_aead_encrypt()` or `crypto_aead_decrypt()` according to `encrypt`.
5. Return that operation through `crypto_wait_req()`.

The scatterlist is used as both source and destination.
Encryption processes 16 plaintext bytes and appends the 16-byte tag.
Decryption processes all 32 bytes and returns 16 plaintext bytes.

## Verify

```sh
./rootkitlings check L50
```

The check reads the real `/proc/rk_aead` result and requires:

- the exact known ciphertext and tag;
- the original plaintext after decryption;
- `-EBADMSG` after one encrypted byte is changed;
- clean removal of the proc entry.

## Limits

Authenticated encryption protects message content and detects modification.
It does not hide endpoints, timing, message size, packet structure, or keys held in memory.
