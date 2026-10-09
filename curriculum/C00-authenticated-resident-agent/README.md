# Authenticate and independently verify the resident agent

This capstone extends your remote resident agent milestone.
The agent still connects only to a loopback TCP listener and still accepts only `PING`, `INFO`, `MARK label`, and `QUIT`.
It does not run shell text or arbitrary commands.

The starting workspace is your completed remote resident agent plus the new crypto, signal, and `security/control.c` files.
Make seven focused changes:

- implement `rk_secure_channel_start()`, `rk_secure_channel_stop()`,
  `rk_secure_open_request()`, and `rk_secure_seal_reply()` in
  `security/control.c`;
- add the secure-channel state and start parameters in `include/agent.h`;
- open requests before parsing and seal replies before framing in
  `network/agent.c`;
- expose the shared host types in `include/framing.h` and the HMAC operations in
  `include/platform.h`;
- start the channel with a nonzero session, send the separate status signal,
  and unwind both failure and unload paths in `core/main.c`;
- compile the new components in `Kbuild`.

The authenticated envelope is the new mechanism.
The other six files connect it to the supplied resident-agent, AES-256-GCM, and authenticated-signal components; do not reimplement those components.

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

The supplied keys are test fixtures, not a key-provisioning design.

## Separate status signal

After every resident-agent resource and endpoint is ready, the module sends one DNS-shaped status datagram from the authenticated DNS signal lesson to `127.0.0.1:5300`.
Its authenticated body reports version 1, the control session as its epoch, sequence 1, and ready status 2.

DNS-shaped does not mean ordinary DNS.
The name, timing, length, session, sequence, and status remain visible.
The checker captures the datagram and verifies its structure and HMAC independently; it does not trust module status.

## Evidence

The checker:

- uses OpenSSL as an independent AES-GCM oracle;
- changes every byte of an authenticated request and verifies no plaintext or
  replay state is released;
- checks wrong-key, truncation, duplicate, gap, crypto-failure, exhaustion, and
  cleanup behavior;
- exercises the real loopback agent, rejected replay and tamper, reconnect,
  blocked-receive cancellation, boot-request rollback, and the captured status
  signal in Linux 7.2.9;
- forces the final signal step to fail and verifies the complete resident-agent
  graph rolls back;
- reruns the full resident-agent, authenticated-encryption, and
  authenticated-signal regression suites.

## References

- Linux v7.2 [`include/crypto/aead.h`](https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h)
- [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final)
- [RFC 5116: AEAD interface and nonce requirements](https://www.rfc-editor.org/rfc/rfc5116.html)
- [RFC 1035: DNS message format](https://www.rfc-editor.org/rfc/rfc1035.html)
