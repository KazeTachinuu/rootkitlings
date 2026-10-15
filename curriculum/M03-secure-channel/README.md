# Encrypted envelope over the agent connection

This milestone wires together all four crypto lessons (L18 through L21) into the
callback agent from M02.
Every message on the wire is now framed, encrypted, and authenticated.
An observer running tcpdump sees encrypted gibberish.
A plain netcat connection is rejected at the handshake.

## Architecture

```text
  Operator (nc/python)            Kernel Agent
        |                              |
        |  <--- TCP connect --->       |
        |                              |
        |  [frame: envelope(HELLO)]    |
        |  <----------------------     |
        |                              |
        |  [frame: envelope(HELLO)]    |
        |  ---------------------->     |
        |                              |
        |  [frame: envelope(cmd)]      |
        |  ---------------------->     |
        |                              |
        |  [frame: envelope(reply)]    |
        |  <----------------------     |
        |         ...                  |

  Each frame:
  +------+-------------------------------------------+
  | len  | envelope_hdr | ciphertext | tag           |
  +------+-------------------------------------------+
    2B         18B          N bytes     16B
```

## Layer composition

1. **Framing (L18)**: 2-byte length prefix wraps every message.
   Handles short writes and short reads.

2. **AEAD (L19)**: AES-256-GCM encrypts the payload and produces a 16-byte
   authentication tag.
   Tampered messages fail with -EBADMSG.

3. **Envelope (L20)**: 18-byte header (version, direction, session_id, sequence)
   serves as AAD.
   Nonce constructed from session + direction + sequence.
   Replayed messages rejected.

4. **Key (L21)**: 256-bit key loaded from module_param_string at insmod.
   Zeroized on unload.

## Handshake

After TCP connect, both sides exchange encrypted HELLO messages.
This confirms they share the same key before entering the command loop.
If decryption fails (wrong key, tampering), the agent closes the socket and
retries after a delay.

## Demo

```sh
# Terminal 1: start a listener
nc -l 4444

# Terminal 2: load the module with a key
insmod rklings.ko control_key=$(head -c32 /dev/urandom | xxd -p -c64)

# Terminal 3: observe encrypted traffic
tcpdump -A -i lo port 4444
# Output: encrypted gibberish (compare with plaintext from M02)
```

## Your task

Edit `crypto/envelope.c`:

1. Implement `rk_send_sealed`: build envelope header, seal with AEAD, send as a
   framed message.
2. Implement `rk_recv_sealed`: receive frame, check replay, open with AEAD.

Edit `network/protocol.c`:

3. Implement framed send/receive with 2-byte length prefix.

Edit `network/agent.c`:

4. Implement `rk_handshake`: exchange encrypted HELLO messages.
5. Implement `rk_handle_session`: receive/send loop with encrypted frames.
6. Implement `rk_agent_thread`: connect, handshake, handle session, retry.

Edit `core/init.c`:

7. Parse key from module parameter, start agent thread, clean up on exit.

Run:

```sh
./rklings check
```

## References

- [Kernel AEAD API](https://docs.kernel.org/7.2/crypto/api-aead.html)
- [kernel_sendmsg / kernel_recvmsg](https://elixir.bootlin.com/linux/v7.2/source/net/socket.c)
- [kthread API](https://elixir.bootlin.com/linux/v7.2/source/include/linux/kthread.h)
