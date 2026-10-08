# 51 - Verify data with SHA-256

Suppose a module expects a specific configuration, payload, or file.
It can store a small trusted fingerprint instead of storing a second complete copy.

```text
received data ----> SHA-256 ----> computed digest
                                      |
trusted digest -----------------------+----> equal: accept
                                      |
                                      +----> different: reject
```

SHA-256 accepts any number of input bytes and always returns a 32-byte digest.
Changing even one input byte produces a different digest.

## Concrete result

The trusted input is:

```text
rootkitlings verification data
```

Its SHA-256 digest is:

```text
5a3faae9795848835c7647c58a9843cf424811d37712aa6340e48e35c28d6dec
```

The 32 binary bytes are displayed as 64 hexadecimal characters because each byte needs two hex digits.

```sh
printf 'rootkitlings verification data' | sha256sum
```

## Kernel hash objects

The synchronous hash API uses a transform and a per-operation descriptor:

```text
crypto_alloc_shash("sha256")
              |
              v
     struct crypto_shash
              |
              +----> descriptor + private scratch space
                              |
                              v
                   crypto_shash_digest()
                              |
                              v
                       32-byte digest
```

`crypto_shash_descsize()` reports how much private scratch space the selected implementation needs.
Allocate that space immediately after `struct shash_desc`:

```c
sizeof(*desc) + crypto_shash_descsize(tfm)
```

Set `desc->tfm` before submitting the digest operation.
Free both allocations on every success and failure path.

## Compute the digest

```sh
./rootkitlings start L51
```

Complete `compute_sha256()`:

1. Allocate `sha256` with `crypto_alloc_shash()` and handle `IS_ERR()`.
2. Allocate the descriptor and its scratch space with `kmalloc()`.
3. Assign the transform to `desc->tfm`.
4. Hash `data` into `out` with `crypto_shash_digest()`.
5. Free the descriptor and transform, then return the operation status.

The surrounding code compares the result with `crypto_memneq()`.
Unlike an ordinary early-exit comparison, its execution does not reveal which byte differed.

`/proc/rk_hash` exposes the result:

```text
algo=sha256
status=ok
hash=5a3faae9795848835c7647c58a9843cf424811d37712aa6340e48e35c28d6dec
```

Writing different data recomputes the digest and changes the status:

```sh
printf changed > /proc/rk_hash
cat /proc/rk_hash
```

```text
status=failed
hash=d67e2e944994496c8d8ec76eed0cf9f09679448d584b532bebf941852a37f5ed
```

## Verify

```sh
./rootkitlings check L51
```

The check requires the exact SHA-256 digest for both inputs, the correct verification status, and clean procfs removal.

## Trust boundary

An unkeyed hash detects a change only when the expected digest comes from a trusted source.
An attacker who can replace both the data and its expected digest can create a matching pair.
Use HMAC when two parties share a secret key, or a digital signature when verification must not require the signing key.
