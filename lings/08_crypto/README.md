# Crypto

Encrypt communication. Without encryption, traffic is readable in `tcpdump`.
Your rootkit works without crypto, but a defender watching the wire sees every command in cleartext.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 57 | XOR obfuscation | XOR encode strings so `strings hello.ko` reveals nothing. | XOR is trivially reversible. Known plaintext breaks it instantly. |
| 58 | AES encryption | `crypto_alloc_aead` + AES-GCM encrypt/authenticate one message. | Without a nonce, identical plaintexts produce identical ciphertexts. Use `MODULE_SOFTDEP` for crypto algorithm dependencies. |
| 59 | Hash verification | Sequence numbers, counter-based nonces, reject replay. | Without replay protection, captured packets can be re-sent to re-execute commands. |

Ling 57 is the turning point: run `strings hello.ko` on a module with a hardcoded
password and see it in cleartext. XOR hides it from casual inspection, but any
defender with the binary can reverse it.

Ling 58 requires `MODULE_SOFTDEP("pre: aes gcm ghash_generic")` to ensure the
kernel crypto modules are loaded. Without it, `crypto_alloc_aead("gcm(aes)")`
fails silently on minimal kernel configs.

After ling 59: build [Capstone 5: Red vs blue](../../capstones/5-red-vs-blue/README.md).
