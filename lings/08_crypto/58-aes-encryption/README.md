# 58 - AES encryption

## What we are learning

XOR only obscures bytes; AES provides actual confidentiality. This lesson uses
the kernel crypto API directly. AES-CBC does not authenticate the ciphertext,
so the exercise demonstrates encryption, not a complete secure protocol.

```
  Encryption:
  +------------+     +----------+     +--------------+
  | plaintext  | --> | AES-CBC  | --> | ciphertext   |
  | "rootkit"  |     | key + IV |     | 7a 3f 91 ... |
  +------------+     +----------+     +--------------+

  Decryption:
  +--------------+     +----------+     +------------+
  | ciphertext   | --> | AES-CBC  | --> | plaintext  |
  | 7a 3f 91 ... |     | key + IV |     | "rootkit"  |
  +--------------+     +----------+     +------------+
```

AES-CBC chains each block's encryption to the previous one, so
identical plaintext blocks produce different ciphertext. ECB mode
does not chain, which leaks patterns.

## What you already know

From ling 57: XOR obfuscation, hex encoding for proc output.

## New API

```c
#include <crypto/skcipher.h>
#include <linux/scatterlist.h>
```

| API | Purpose |
|-----|---------|
| `crypto_alloc_sync_skcipher("cbc(aes)", 0, 0)` | Allocate an AES-CBC cipher |
| `crypto_sync_skcipher_setkey(tfm, key, len)` | Set the encryption key |
| `skcipher_request_alloc(&tfm->base, GFP_KERNEL)` | Allocate a request |
| `skcipher_request_set_crypt(req, &sg, &sg, len, iv)` | Bind buffer and IV |
| `crypto_skcipher_encrypt(req)` | Encrypt in place |
| `crypto_skcipher_decrypt(req)` | Decrypt in place |
| `sg_init_one(&sg, buf, len)` | Initialize a scatterlist from a buffer |

`sg_init_one()` needs a linear buffer with a valid backing page for the entire
request lifetime. This exercise uses a `kmalloc` buffer, which satisfies that
contract and remains alive until the synchronous operation returns. Do not pass
`vmalloc` memory as one scatterlist entry or let a temporary buffer expire while
an asynchronous request is still active.

## Build it

Create a module that:

1. Loads the one-block AES-128-CBC test vector from NIST SP 800-38A.
2. `kmalloc`s a buffer and copies the plaintext into it.
3. Encrypts with the vector's key and IV.
4. Compares the ciphertext with the published answer.
5. Decrypts it and compares the result with the original plaintext.
6. `/proc/rk_aes` reports both comparisons.

The data size must be a multiple of 16 (AES block size). Pad your
plaintext to 16 bytes if needed.

Free the cipher with `crypto_free_sync_skcipher`, the request with
`skcipher_request_free`, and the buffer with `kfree`.

Fixed keys and IVs are correct only for a reproducible known-answer test. In a
real protocol, an IV is not secret, but it must not be reused with the same CBC key. CBC
provides confidentiality only; authenticated protocols should use an AEAD mode
such as AES-GCM instead of combining primitives casually.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_aes                 # algo, status, plain, cipher, match
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- The plaintext and ciphertext match the published AES-128-CBC vector.
- `/proc/rk_aes` contains `known_answer=yes` and `match=yes`.
- `match=yes` confirms decrypt(encrypt(plain)) equals the original.
- `/proc/rk_aes` disappears after unload.

## Verify

```bash
./rootkitlings check 58
```

The check verifies the exact known answer and the decrypt round trip.

> **What a defender sees**: this exercise embeds its test key, IV, and plaintext
> in the module. Encryption does not conceal constants already present in the
> `.ko`; `strings` and binary inspection remain relevant.

See also: [59 - Hash verification](../59-hash-verification/README.md).
