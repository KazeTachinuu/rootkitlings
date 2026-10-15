# Download a file from the target

The rootkit can open a shell, but sometimes you just need one file.
Exfiltrating `/etc/shadow` or a database dump does not require an interactive
session.
This Read a file from the kernel using `filp_open` and `kernel_read`, then sends
it over the encrypted channel in length-prefixed chunks.

## Reading files from kernel space

The kernel provides file operations that mirror userspace `open`/`read`/`close`:

```text
filp_open(path, O_RDONLY, 0)
      |
      v
kernel_read(filp, buf, chunk_size, &pos)   <-- loop until 0
      |
      v
filp_close(filp, NULL)
```

These are the same functions the kernel uses internally for firmware loading and
other file I/O.
They work on any file the kernel can access.

## Chunked transfer

A file may be larger than a single network frame.
Send it in chunks:

1. Open the file with `filp_open`.
2. Allocate a 4096-byte read buffer.
3. Loop: `kernel_read` into the buffer, send each chunk as a framed and
   encrypted message.
4. When `kernel_read` returns 0 (EOF), send a zero-length frame to signal the
   end of the transfer.
5. Close the file with `filp_close`.

The 4096-byte chunk size matches the frame maximum from the secure channel,
keeping each chunk within a single encrypted frame.

```text
[chunk 1: 4096 bytes] --> encrypt --> frame --> send
[chunk 2: 4096 bytes] --> encrypt --> frame --> send
...
[chunk N: remaining]  --> encrypt --> frame --> send
[zero-length frame]   --> encrypt --> frame --> send (EOF)
```

## Error handling

If the file does not exist or cannot be opened, `filp_open` returns an
`ERR_PTR`.
Send an error frame back to the operator instead of file data:

```c
struct file *f = filp_open(path, O_RDONLY, 0);
if (IS_ERR(f)) {
    /* Send error frame with PTR_ERR(f) as the payload */
    return PTR_ERR(f);
}
```

The operator-side tool interprets a negative value in the first frame as an
error code.

## Complement to M00

In M00 you used `filp_open` with `O_WRONLY | O_CREAT` and `kernel_write` to
persist data to disk.
Here the direction reverses: `O_RDONLY` and `kernel_read` to exfiltrate data off
the target.

## Your task

Edit `main.c`:

1. Implement `rk_download_file(sock, ctx, filepath)`:
   - Open the file with `filp_open(filepath, O_RDONLY, 0)`.
   - Allocate a 4096-byte buffer with `kmalloc`.
   - Loop with `kernel_read(filp, buf, 4096, &pos)`.
   - Send each chunk as a framed, encrypted message.
   - Send a zero-length frame after the last chunk.
   - Close the file with `filp_close`.
2. Handle errors: send an error frame if `filp_open` fails.
3. Free the buffer and close the file in all paths (success and error).

Run:

```sh
./rklings check
```

## References

- [`filp_open`](https://github.com/torvalds/linux/blob/v7.2/fs/open.c)
- [`kernel_read`](https://github.com/torvalds/linux/blob/v7.2/fs/read_write.c)
- [`filp_close`](https://github.com/torvalds/linux/blob/v7.2/fs/open.c)
