# Complete and verify the course rootkit

This final challenge completes one module with:

- bounded local control and resident work;
- one-use credential replacement;
- syscall-result telemetry;
- process-view filtering with an independent evidence view;
- a guarded boot-load request;
- an authenticated, replay-safe loopback control channel;
- an authenticated DNS-shaped ready signal;
- rollback for every startup step and complete unload cleanup.

The remote agent accepts only `PING`, `INFO`, `MARK label`, and `QUIT`.
It does not run shell text or arbitrary commands.
The supplied keys are test fixtures, not a key-provisioning design.

Edit:

- `Kbuild`
- `core/main.c`
- `include/agent.h`
- `include/framing.h`
- `include/platform.h`
- `network/agent.c`
- `security/control.c`

Implement the four secure-channel operations in `security/control.c`.
Store the channel state in the agent, authenticate each request before parsing, authenticate each reply before framing, send the ready signal only after every resource is available, and release resources in reverse order on failure and unload.
Compile the supplied AES-256-GCM and authenticated-signal components without reimplementing them.

Run:

```sh
./rootkitlings check C00
```

## Control envelope

Each resident-agent TCP frame carries one envelope:

```text
version:1 | kind:1 | session:8 | sequence:8 | ciphertext | tag:16
```

The first 18 bytes remain visible and are passed to AES-GCM as associated data.
`kind` is 1 for a request and 2 for a reply.
Integers use big-endian order.
Plaintext is at most 30 bytes, so the complete envelope remains within the resident agent's 64-byte frame limit.

Associated data keeps the header visible while making any change fail authentication.
Reusing a GCM nonce under one key breaks its security, so the session separates module loads, the direction bit separates requests from replies, and the sequence separates messages.
Exact sequence checks also reject a recorded envelope instead of running its command again.

For each direction, sequence numbers start at 1 and must arrive exactly in order.
A duplicate is replay; a larger value is a gap.
Neither changes state.
The largest permitted sequence is `0x7fffffff`.

Derive the 12-byte nonce as:

```text
session:8 | ((sequence << 1) | direction):4
```

`direction` is 0 for a request and 1 for a reply.
This gives request and reply different nonces even when they have the same sequence number.
`session_id` must be nonzero and new for every module load under the same key.
The course checker supplies it explicitly so the independent peer can use the same value.

Open into private storage first.
Publish plaintext and advance the request sequence only after the tag, visible header, session, and exact sequence all pass.
Seal into private storage first.
Publish the envelope and advance the reply sequence only after AES-GCM succeeds.
Clear private plaintext, envelope, nonce, and channel key storage after use.

## Separate status signal

After every resident-agent resource and endpoint is ready, the module sends one DNS-shaped status datagram from the authenticated DNS signal lesson to `127.0.0.1:5300`.
Its authenticated body reports version 1, the control session as its epoch, sequence 1, and ready status 2.

DNS-shaped does not mean ordinary DNS.
The name, timing, length, session, sequence, and status remain visible.
The checker captures the datagram and verifies its structure and HMAC independently; it does not trust module status.

## Evidence

The checks run against your completed C00 module.
They:

- use OpenSSL as an independent AES-GCM oracle;
- change every byte of an authenticated request and verify no plaintext or
  replay state is released;
- check wrong-key, truncation, duplicate, gap, crypto-failure, exhaustion, and
  cleanup behavior;
- exercise the real loopback agent, rejected replay and tamper, reconnect,
  blocked-receive cancellation, boot-request rollback, and the captured status
  signal in Linux 7.2.9;
- exercise local control, credential replacement, syscall telemetry,
  process-view filtering, and the evidence view;
- force every startup failure point from 1 through 14 and verify rollback;
- reject an unknown failure point at build time.

## References

- Linux v7.2 [`include/crypto/aead.h`](https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h)
- [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final)
- [RFC 5116: AEAD interface and nonce requirements](https://www.rfc-editor.org/rfc/rfc5116.html)
- [RFC 1035: DNS message format](https://www.rfc-editor.org/rfc/rfc1035.html)
