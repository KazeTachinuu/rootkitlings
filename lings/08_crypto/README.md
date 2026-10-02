# Crypto

Encrypt communication. Without encryption, traffic is readable in `tcpdump`.
Your rootkit works without crypto, but a defender watching the wire sees every command in cleartext.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 55 | Key derivation | `crypto_alloc_shash` + HMAC-based KDF from password + salt. | Hardcoded password is extractable from the `.ko` binary with `strings`. |
| 56 | AEAD | `crypto_alloc_aead` + AES-GCM encrypt/authenticate one message. | Without a nonce, identical plaintexts produce identical ciphertexts. Use `MODULE_SOFTDEP` for crypto algorithm dependencies. |
| 57 | Protocol state | Sequence numbers, counter-based nonces, reject replay. | Without replay protection, captured packets can be re-sent to re-execute commands. Counter vs random nonces: tradeoffs in collision probability vs state synchronization. |

Authentication and encrypted transport are covered here.

Ling 53 is the turning point: run `strings hello.ko` on a module with a hardcoded
password and see it in cleartext. Then derive the key from a parameter-supplied
password at load time. The password never lives in the binary.

Ling 54 requires `MODULE_SOFTDEP("pre: aes gcm ghash_generic")` to ensure the
kernel crypto modules are loaded. Without it, `crypto_alloc_aead("gcm(aes)")`
fails silently on minimal kernel configs.

After ling 57: build [Capstone 5: Red vs blue](../../capstones/5-red-vs-blue/README.md).
