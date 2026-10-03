# 59 - Hash verification

## What we are learning

SHA-256 gives a stable fingerprint: 32 bytes out for any input. Verification
means computing that fingerprint and comparing it with a trusted expected
digest using a timing-safe comparison.

```
  input (any length)         SHA-256           output (always 32 bytes)
  +-----------------+     +----------+     +----------------------------------+
  | "rootkitlings   | --> | compress | --> | 5a3faae9795848835c7647c58a9843cf |
  |  verification   |     | rounds   |     | 424811d37712aa6340e48e35c28d6dec |
  |  data"          |     +----------+     +----------------------------------+
  +-----------------+          64 hex characters
```

This is the last ling in the crypto section. You now have XOR (ling 57),
AES (ling 58), and SHA-256: obfuscation, encryption, and integrity.

## What you already know

From ling 58: the kernel crypto API pattern (allocate, configure, use,
free). SHA-256 uses the synchronous hash API instead of the symmetric
cipher API, but the structure is the same.

## New API

```c
#include <crypto/hash.h>
```

| API | Purpose |
|-----|---------|
| `crypto_alloc_shash("sha256", 0, 0)` | Allocate a SHA-256 transform |
| `crypto_shash_descsize(tfm)` | Size of the descriptor scratch space |
| `crypto_shash_digest(desc, data, len, out)` | Hash in one call (init + update + final) |
| `crypto_free_shash(tfm)` | Free the transform |

The descriptor (`struct shash_desc`) needs `kmalloc` with extra space:
`sizeof(*desc) + crypto_shash_descsize(tfm)`. Set `desc->tfm = tfm`
before calling digest.

## Build it

Create a module that:

1. Computes the SHA-256 hash of the string `rootkitlings verification data`.
2. Formats the 32-byte digest as 64 hex characters.
3. Compares it with the trusted digest using `crypto_memneq`.
4. `/proc/rk_hash` shows the algorithm, verification status, and hex hash.

The expected hash is `5a3faae9795848835c7647c58a9843cf424811d37712aa6340e48e35c28d6dec`.
You can verify this on the host: `echo -n "rootkitlings verification data" | sha256sum`.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_hash                 # algo, input, hash (hex), verified status
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_hash` contains `algo=sha256`, `status=ok`, and `hash=` (64 hex chars).
- Writing new data updates the hash and changes verification to `failed`.
- `/proc/rk_hash` disappears after unload.

## Verify

```bash
./rootkitlings check 59
```

The check verifies `status=ok` and that the hash is exactly 64
hexadecimal characters.

> **Security boundary**: an unkeyed hash does not authenticate commands; an
> attacker who can replace both data and digest can forge the check. Use HMAC
> for shared-key authentication or a digital signature for independently
> verifiable artifacts.
>
> **What a defender sees**: a module using the kernel crypto API for
> SHA-256 is not inherently suspicious (many legitimate modules do).
> But combined with the techniques from earlier lings, it completes
> a rootkit's crypto toolkit.

All lings complete. Next: [Capstone 5 - Red vs blue](../../../capstones/5-red-vs-blue/README.md).
