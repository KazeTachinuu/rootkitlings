# Build the secure envelope with replay protection

Raw AEAD encrypts messages, but says nothing about ordering or freshness.
An attacker who captures ciphertext can replay it later.
Build an envelope that binds each message to a session, a direction, and a
sequence number, making replays detectable.

## Envelope wire format

```text
+----+-----+------------+------------+-----------+-----+
| V  | Dir | session_id |  sequence  | ciphertext| tag |
+----+-----+------------+------------+-----------+-----+
  1     1       8            8          pt_len      16

V   = version (always 1)
Dir = 0 (client-to-server) or 1 (server-to-client)
```

The 18-byte header is not encrypted.
It is passed as AAD (Additional Authenticated Data) to the AEAD, so any
modification to the header causes decryption to fail with -EBADMSG.

## Nonce construction

GCM requires a 12-byte nonce that must never repeat for the same key.
Construct it deterministically:

```text
+-------------------+-----+----------+
|    session_id     | dir | seq[2:0] |
+-------------------+-----+----------+
      8 bytes         1       3 bytes

session_id : random per-connection, ensures different sessions use different nonces
dir        : prevents client and server from reusing the same nonce
seq[2:0]   : low 3 bytes of the sequence counter
```

This guarantees uniqueness as long as no direction sends more than 2^24 messages
per session with the same key.
For a rootkit C2 channel, that is more than sufficient.

## Replay protection

Each direction maintains a sequence counter starting at 0.
The sender increments after each message.
The receiver tracks the next expected sequence number and rejects any message
with a sequence below the expected value:

```text
Sender: seq 0 -> seq 1 -> seq 2 -> ...
Receiver expects: 0, then 1, then 2, ...

Replay of seq 0 after seq 2 received: rejected (-EPROTO)
```

## Data structures

```c
struct rk_envelope_hdr {
    u8 version;
    u8 direction;
    __be64 session_id;
    __be64 sequence;
} __packed;

struct rk_envelope_ctx {
    struct crypto_aead *tfm;
    u64 session_id;
    u8 direction;
    u64 tx_seq;
    u64 rx_seq_expected;
};
```

## Your task

Edit `main.c`:

1. Implement `rk_build_nonce`: construct 12-byte nonce from session_id,
   direction, and sequence.
2. Implement `rk_envelope_seal`: build header, construct nonce, encrypt with
   header as AAD, output header + ciphertext + tag.
3. Implement `rk_envelope_open`: parse header, check replay, reconstruct nonce,
   decrypt with header as AAD.
4. Complete the self-test: seal "test", open it, replay it (expect -EPROTO).

Run:

```sh
./rklings check
```

## References

- [GCM nonce requirements](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38d.pdf)
- [AEAD AAD usage](https://docs.kernel.org/7.2/crypto/api-aead.html)
