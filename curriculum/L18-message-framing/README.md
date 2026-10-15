# Frame messages on a TCP stream

TCP is a byte stream, not a message protocol.
Two sends of 100 bytes each may arrive as a single 200-byte read, or as three
reads of 30, 70, and 100 bytes.
Before we can encrypt individual messages with AEAD, we need message boundaries.

Implement a 2-byte length-prefix framing protocol in kernel space.

## Why framing comes first

AEAD encrypts and authenticates discrete messages.
Without framing, the receiver cannot tell where one ciphertext ends and the next
begins.
The length prefix solves this: every message is preceded by its size, so the
receiver always knows exactly how many bytes to read.

## Wire format

```text
+--------+--------+-------------------+
| len[0] | len[1] |   payload bytes   |
+--------+--------+-------------------+
   2 bytes (BE)       len bytes

Example: "PING" (4 bytes)
  00 04 50 49 4e 47
  ^^^^               length = 4 (big-endian)
       ^^^^^^^^^^^^^  payload = "PING"
```

Zero-length frames are valid: just the 2-byte header with value 0x0000.
Maximum frame size: 4096 bytes.

## Short writes and short reads

`kernel_sendmsg` and `kernel_recvmsg` are not guaranteed to transfer the full
requested length on a TCP socket.
The kernel may transfer only part of the data, returning the number of bytes
actually transferred.
Your code must loop:

```c
while (sent < len) {
    n = kernel_sendmsg(sock, &msg, &vec, 1, remaining);
    if (n < 0) return n;
    if (n == 0) return -ECONNRESET;
    sent += n;
}
```

The same pattern applies to `kernel_recvmsg`.
A return of 0 means the peer closed the connection.

## Functions to implement

1. `send_exact(sock, buf, len)`: retry loop for short writes.
2. `recv_exact(sock, buf, len)`: retry loop for short reads.
3. `send_frame(sock, buf, len)`: write 2-byte BE length, then payload.
4. `recv_frame(sock, buf, max_len, out_len)`: read 2-byte length, then payload.

## Testing

Start a listener on the host:

```sh
nc -l 4444 | xxd -l 20
```

Load the module.
The first bytes on the wire should be:

```
00 04 50 49 4e 47
```

That is: length 4 (big-endian), followed by "PING".

## Your task

Edit `main.c`:

1. Implement `send_exact` with a retry loop for short writes.
2. Implement `recv_exact` with a retry loop for short reads.
3. Implement `send_frame` to write the 2-byte length header then the payload.
4. Implement `recv_frame` to read the header and then the exact payload.

Run:

```sh
./rklings check
```

## References

- [`kernel_sendmsg`](https://elixir.bootlin.com/linux/v7.2/source/net/socket.c)
- [`kernel_recvmsg`](https://elixir.bootlin.com/linux/v7.2/source/net/socket.c)
- [htons/ntohs](https://elixir.bootlin.com/linux/v7.2/source/include/linux/byteorder/generic.h)
