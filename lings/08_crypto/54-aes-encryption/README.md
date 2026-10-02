# 54 - AES encryption

## What we are learning

Real rootkits use AES (not XOR) to protect C2 communications and stored
secrets. The Linux kernel has a full crypto API with hardware-accelerated
AES, no external libraries needed.

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

From ling 53: XOR obfuscation, hex encoding for proc output.

## New API

```c
#include <crypto/skcipher.h>
#include <linux/scatterlist.h>
#include <linux/random.h>
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

Important: scatterlist buffers must be `kmalloc`'d (not stack or static).
The crypto API uses DMA, and only heap memory has valid physical page
mappings for scatterlist.

## Build it

Create a module that:

1. Generates a random 128-bit key with `get_random_bytes`.
2. `kmalloc`s a buffer, copies your plaintext into it.
3. Encrypts it in place with AES-CBC (zero IV is fine for a demo).
4. Saves the ciphertext, then decrypts back and verifies the round trip.
5. `/proc/rk_aes` shows hex-encoded plain, cipher, and whether they match.

The data size must be a multiple of 16 (AES block size). Pad your
plaintext to 16 bytes if needed.

Free the cipher with `crypto_free_sync_skcipher`, the request with
`skcipher_request_free`, and the buffer with `kfree`.

## Predict

- Why must the scatterlist buffer be `kmalloc`'d?
- What happens if you reuse the same IV for two encryptions?
- Why AES-CBC instead of AES-ECB?


## Hints

<details>
<summary>Hint 1: the buffer MUST be kmalloc'd</summary>

The crypto API uses scatterlists, which require valid physical page
mappings. Stack buffers and static globals do not have these. Allocate
your plaintext/ciphertext buffer with `kmalloc`:

```c
buf = kmalloc(16, GFP_KERNEL);  // AES block = 16 bytes
```

Copy your plaintext into this buffer, then encrypt in place.

</details>

<details>
<summary>Hint 2: the setup sequence</summary>

The crypto API has a strict setup order:

1. `crypto_alloc_sync_skcipher("cbc(aes)", 0, 0)` to get a transform.
2. `crypto_sync_skcipher_setkey(tfm, key, keylen)` to set the key.
3. `skcipher_request_alloc(&tfm->base, GFP_KERNEL)` to get a request.
4. `sg_init_one(&sg, buf, len)` to wrap your buffer.
5. `skcipher_request_set_crypt(req, &sg, &sg, len, iv)` to bind it.
6. `crypto_skcipher_encrypt(req)` to encrypt.

The request and transform are separate objects. Free them in reverse
order.

</details>

<details>
<summary>Hint 3: the IV is consumed during encryption</summary>

AES-CBC modifies the IV buffer during encryption. If you need to
decrypt the ciphertext, you must reset the IV to its original value
(e.g., all zeros) before calling `crypto_skcipher_decrypt`. Without
resetting, decryption produces garbage.

</details>

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

- `/proc/rk_aes` contains `algo=aes-cbc`, `status=ok`, `plain=`, `cipher=`, and `match=yes`.
- Ciphertext differs from plaintext (encryption happened).
- `match=yes` confirms decrypt(encrypt(plain)) equals the original.
- `/proc/rk_aes` disappears after unload.

## Verify

```bash
./rootkitlings check 54
```

The check verifies `status=ok` and `match=yes` (round-trip success).

> **What a defender sees**: the module binary contains no plaintext
> secrets if AES is used properly. But the key must be stored somewhere:
> extracting it from kernel memory or the `.ko` file breaks the scheme.

See also: [55 - Hash verification](../55-hash-verification/README.md).
