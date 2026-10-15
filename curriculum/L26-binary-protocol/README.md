# Length-framed binary protocol

Text commands over a raw TCP stream have a fundamental problem: the receiver
cannot distinguish one command from the next without scanning for newlines, and
a newline inside a payload would split the message. This lesson replaces the
newline-delimited protocol with length-framed binary messages.

## Wire format

Every message (command or response) is prefixed with a 2-byte big-endian length
header, followed by exactly that many bytes of payload:

```text
+--------+--------+-----------------------------+
| len_hi | len_lo |         payload             |
+--------+--------+-----------------------------+
   2 bytes            len bytes (big-endian)
```

The text commands inside the payload remain unchanged. The framing layer is
transparent: `INFO\n` becomes `\x00\x05INFO\n` on the wire (length 5, then the
five payload bytes).

## Sending a framed message

Before sending the payload, send its length as a 2-byte big-endian integer:

```c
u16 frame_len = htons((u16)len);
/* Send the 2-byte header */
kernel_sendmsg(sock, &msg, &hdr_vec, 1, 2);
/* Then send the payload */
kernel_sendmsg(sock, &msg, &payload_vec, 1, len);
```

`htons` converts a host-order `u16` to network byte order (big-endian).

## Receiving a framed message

Read exactly 2 bytes to get the length header, convert with `ntohs`, then read
exactly that many bytes as the command payload:

```c
/* Read 2-byte length header */
n = rk_recv_exact(sock, &frame_len, 2);
if (n != 2)
    return n;
payload_len = ntohs(frame_len);

/* Read the payload */
n = rk_recv_exact(sock, buf, payload_len);
```

The `rk_recv_exact` helper must loop until all requested bytes arrive or an
error occurs, because `kernel_recvmsg` may return fewer bytes than requested
(short reads are normal on TCP).

## Observing the framing

After implementing the binary protocol, use `hexdump` or `xxd` on a packet
capture to see the 2-byte length prefix before each command and response:

```text
$ echo -ne '\x00\x05INFO\n' | nc localhost 4444 | xxd
00000000: 0042 6e6f 6465 6e61 6d65 3a20 ...  .Bnodename: ...
           ^--- 0x0042 = 66 bytes of response
```

## Your task

Edit `main.c`:

1. Modify `rk_send` to prepend a 2-byte big-endian length header before the
   payload.
2. Implement `rk_recv_exact`: a helper that loops `kernel_recvmsg` until
   exactly `len` bytes have been read, or an error occurs.
3. Implement `rk_recv_frame`: read 2 bytes as the length header, convert with
   `ntohs`, then call `rk_recv_exact` for the payload. Null-terminate the
   result.
4. Update `rk_handle_client` to use `rk_recv_frame` instead of the raw recv
   call.

Run:

```sh
./rklings check
```

## References

- [`htons` / `ntohs` byte order conversion](https://github.com/torvalds/linux/blob/v7.2/include/linux/byteorder/generic.h)
- [TCP stream semantics and short reads](https://docs.kernel.org/7.2/networking/msg_zerocopy.html)
