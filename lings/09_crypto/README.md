# Cryptography

Obfuscation, confidentiality, integrity, authentication, and rendezvous naming solve different problems.

| # | Mechanism | Provides | Does not provide |
|--:|-----------|----------|------------------|
| 49 | XOR transform | Removal of an obvious plaintext string from the module file. | Cryptographic secrecy or integrity. |
| 50 | AES-GCM | Confidentiality and modification detection for one known-answer vector. | Safe nonce generation or key storage. |
| 51 | SHA-256 | An unkeyed fingerprint checked against trusted bytes. | Proof of who created the data. |
| 52 | HMAC-SHA-256 | A keyed tag for message authentication. | Content secrecy. |
| 53 | Keyed domain generation | The same deterministic name for a shared key and time bucket. | Invisible or unobservable DNS traffic. |

```text
remove plaintext signature   XOR
hide and authenticate bytes  AES-GCM
fingerprint trusted bytes    SHA-256
authenticate a message       HMAC-SHA-256
derive a rendezvous name     keyed DGA
```

After ling 53, [Authenticated DNS signal](../../capstones/9-dns-c2/README.md) combines HMAC, a campaign label, DNS framing, and one local UDP datagram.

[Operation Glasshouse](../../capstones/10-red-vs-blue/README.md) is the final lifecycle and cross-view detection exercise.
