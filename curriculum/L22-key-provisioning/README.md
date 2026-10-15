# Provision and verify a shared key

Before the rootkit can authenticate remote connections, it needs a shared secret.
This lesson loads a 256-bit key from a module parameter, parses the hex string
into binary, and computes a short fingerprint for visual verification.

This is the one lesson where `pr_info` output is acceptable: the operator needs
to see the fingerprint in dmesg to confirm the key loaded correctly.

## Key lifecycle

```text
insmod rklings.ko control_key="aabb...64 hex chars..."
        |
        v
  module_param receives charp string
        |
        v
  validate strlen == 64
        |
        v
  hex2bin: 64 hex chars --> 32 bytes
        |
        v
  SHA-256(binary key) --> 32-byte digest
        |
        v
  fingerprint = first 4 bytes of digest, displayed as %08x
        |
        v
  pr_info("rklings: key loaded, fingerprint %08x")
        |
        v
  rk_key_verify(incoming, len) compares 4-byte fingerprint
```

## Module parameters

`module_param` declares a parameter that `insmod` can set at load time.
For a string, use the `charp` type:

```c
static char *control_key;
module_param(control_key, charp, 0);
```

The `0` permission hides the parameter from sysfs (OPSEC).
At init time, `control_key` points to the caller-supplied string or is NULL if
the parameter was omitted.

## Hex parsing

The kernel provides `hex2bin` in `<linux/kernel.h>`:

```c
int hex2bin(u8 *dst, const char *src, size_t count);
```

It reads `2 * count` hex characters from `src` and writes `count` bytes into
`dst`.
Returns 0 on success, -1 on invalid input.
For a 256-bit key, `count` is 32, so `src` must contain exactly 64 hex
characters.

Validate the string length before calling `hex2bin`.
After parsing, zeroize the hex string with `memzero_explicit` so it does not
linger in module parameter memory.

## Key fingerprint

A fingerprint lets the operator confirm both sides share the same key without
revealing the key itself.
Compute the SHA-256 hash of the 32-byte binary key, then take the first 4 bytes
of the digest as a big-endian integer:

```c
u32 fp = (digest[0] << 24) | (digest[1] << 16) |
         (digest[2] << 8)  |  digest[3];
```

Display this with `pr_info("rklings: key loaded, fingerprint %08x\n", fp)`.

Use the kernel crypto shash API for the hash:

```c
struct crypto_shash *tfm = crypto_alloc_shash("sha256", 0, 0);
struct shash_desc *desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm),
                                  GFP_KERNEL);
desc->tfm = tfm;
crypto_shash_digest(desc, key, key_len, digest);
```

## Verification function

Implement a function the agent will call when a client connects:

```c
bool rk_key_verify(const u8 *incoming, size_t len);
```

Returns true only when `len == 4` and the 4 incoming bytes match the stored
fingerprint. Returns false for NULL, wrong length, or mismatched bytes.

## Your task

Edit `main.c`:

1. Declare `control_key` as a `module_param` with type `charp`, permission `0`.
2. In `rk_init`, reject load if `control_key` is NULL or its length is not 64.
3. Parse the hex string into a 32-byte static array with `hex2bin`.
4. Zeroize the hex string after parsing.
5. Compute the SHA-256 fingerprint (first 4 bytes of the key's digest).
6. Print the fingerprint with `pr_info`.
7. Implement `rk_key_verify` to compare an incoming 4-byte fingerprint.
8. In `rk_exit`, zeroize the key and fingerprint with `memzero_explicit`.

Run:

```sh
./rklings check
```

## References

- [`module_param` documentation](https://docs.kernel.org/moduleparam.html)
- [`hex2bin` in kernel](https://github.com/torvalds/linux/blob/v7.2/lib/hexdump.c)
- [Kernel crypto shash API](https://docs.kernel.org/7.2/crypto/api-digest.html)
