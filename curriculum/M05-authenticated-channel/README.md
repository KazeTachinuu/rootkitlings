# Authenticated control channel

M04's callback agent talks in plaintext: anyone who can see the traffic
can read commands and inject their own. This milestone wraps the wire
protocol in AES-256-GCM authenticated encryption, so only the operator
who holds the pre-shared key can issue commands.

## The secure envelope

Every frame between the agent and operator is now an AEAD envelope:

```text
[version:1][kind:1][session:8][sequence:8]  <-- 18-byte header (AAD)
[ciphertext + 16-byte GCM tag]              <-- sealed payload
```

The header is authenticated but not encrypted (the operator needs to
read it to find the right key and nonce). The payload is both encrypted
and authenticated.

## Session and replay defense

```text
  agent                                operator
    |                                     |
    |------ TCP connect ----------------->|
    |                                     |
    |------ HELLO [session, seq=0] ------>|  records session
    |                                     |  rx_seq=1, tx_seq=1
    |                                     |
    |<----- REQUEST [session, seq=1] -----|  agent checks seq
    |------ REPLY   [session, seq=1] ---->|  operator checks seq
    |                                     |
    |<----- REQUEST [session, seq=2] -----|  counters increment
    |------ REPLY   [session, seq=2] ---->|  per direction
    |                                     |
    |<----- REQUEST [session, seq=1] -----|  REJECTED (replay)
```

On each new TCP connection, the agent generates a random 64-bit session
ID and sends a sealed HELLO. The operator records this session and
starts sequence counters at 1. Each side tracks the next expected
sequence number for the other direction: if a frame arrives with the
wrong sequence, it is rejected.

## Nonce construction

The 12-byte GCM nonce is built from three values:

```text
bytes 0-7:  session ID (big-endian u64)
bytes 8-11: (sequence << 1 | direction) (big-endian u32)
```

Direction is 0 for requests (operator to agent) and 1 for replies
(agent to operator). This guarantees nonce uniqueness across directions
and across frames within a session. A new session gets a new random
session ID, so nonces never repeat across sessions.

## Sensitive state zeroization

The control key is wiped from the module parameter buffer after init
with `memzero_explicit()`. The secure channel struct is zeroed on stop.
Key material should never persist in memory longer than necessary.

## Your task

1. In `security/control.c`: implement the secure channel (nonce
   construction, channel start/stop, open_request with replay defense,
   seal_reply with sequence tracking)
2. In `network/agent.c`: implement the authenticated serve loop (HELLO
   handshake, open incoming frames before parsing, seal outgoing replies)
   and the command handler

Run:

```sh
./rklings check
```

## References

- [Linux v7.2 AEAD API][1]
- [Linux v7.2 scatterlist API][2]
- [RFC 5116: AEAD interface][3]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h
[2]: https://github.com/torvalds/linux/blob/v7.2/include/linux/scatterlist.h
[3]: https://www.rfc-editor.org/rfc/rfc5116.html
