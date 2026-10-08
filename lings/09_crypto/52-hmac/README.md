# 52 - Authenticate a message with HMAC

A plain SHA-256 digest does not stop deliberate forgery.
Anyone who can replace a message can calculate the digest of the replacement.

```text
attacker changes command
          |
          +----> calculates a new SHA-256 digest
          |
          +----> message and digest still match
```

HMAC adds a secret key to the calculation.
Only a party with that key can produce the expected tag for a new message.

```text
sender                                     receiver
------                                     --------
message + shared key                       message + shared key
        |                                          |
        v                                          v
   HMAC-SHA-256                                HMAC-SHA-256
        |                                          |
        +---- tag sent with message ---- compare --+
```

The message remains readable.
HMAC authenticates content; it does not encrypt it.

## Known answer

The initial values come from a published HMAC-SHA-256 test vector:

```text
key       0b repeated 20 times
message   Hi There
tag       b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7
```

Changing the message changes the tag while the key remains fixed:

```text
message   command=test
tag       fef31c32f1c026aed366f6458ee649707e94d985b3e9e55d6528ecab11d8682f
```

Exact known answers prove that both the algorithm and key were applied.

## Configure the keyed hash

The synchronous hash API from SHA-256 is reused with a keyed algorithm:

```text
crypto_alloc_shash("hmac(sha256)")
                    |
                    v
crypto_shash_setkey(shared_key)
                    |
                    v
crypto_shash_tfm_digest(message) ----> 32-byte tag
```

```sh
./rootkitlings start L52
```

Complete `configure_hmac()`:

1. Allocate `hmac(sha256)` with `crypto_alloc_shash()`.
2. Return `PTR_ERR()` if allocation returns an error pointer.
3. Install `shared_key` with `crypto_shash_setkey()`.
4. Free the transform if key setup fails.
5. Return the key-setup status.

The transform keeps the key after configuration.
`compute_tag()` can therefore authenticate each bounded message without setting the key again.

`/proc/rk_hmac` shows the current message and tag.
Writing replaces the message and computes a new tag before committing either value:

```sh
printf 'command=test' > /proc/rk_hmac
cat /proc/rk_hmac
```

## Verify

```sh
./rootkitlings check L52
```

The check requires the exact tag for both known messages and verifies clean procfs removal.

## Trust boundary

Both endpoints must receive the shared key through a trusted path.
A key embedded in a module can be recovered from the module file or kernel memory.
HMAC detects messages created without the key; it does not conceal traffic or protect an extractable key.
