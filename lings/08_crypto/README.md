# Crypto

Encrypt communication. Without encryption, traffic is readable in `tcpdump`.
Your rootkit works without crypto, but a defender watching the wire sees every command in cleartext.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 57 | XOR obfuscation | Transform bytes and encode the result as hex. | XOR is reversible and provides no authenticity. |
| 58 | AES-CBC | Use the synchronous skcipher API with a fresh random IV. | CBC provides confidentiality, not authenticity. |
| 59 | SHA-256 verification | Compare a computed digest with a trusted digest. | An unkeyed hash is not command authentication. |

Ling 57 is the turning point: run `strings hello.ko` on a module with a hardcoded
password and see it in cleartext. XOR hides it from casual inspection, but any
defender with the binary can reverse it.

Ling 58 uses `crypto_alloc_sync_skcipher("cbc(aes)", ...)`. Allocation returns
an error pointer when that implementation is unavailable; the module propagates
the error instead of pretending encryption succeeded.

After ling 59: build [Capstone 5: Red vs blue](../../capstones/5-red-vs-blue/README.md).
