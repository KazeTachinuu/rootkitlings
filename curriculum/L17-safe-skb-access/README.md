# Read a packet split across kernel storage

A traffic watcher cannot assume that packet bytes form one flat array.
Linux stores a packet in an `sk_buff`, a container that may divide data across
its main buffer, page fragments, and linked fragments.

Build one bounded reader relative to the network header without changing the
packet.
Success proves that later inspection works across real kernel storage layouts,
not only convenient contiguous buffers.

```text
           head           data                    tail  end
            |              |                        |    |
            v              v                        v    v
main buf:  [  headroom  ][ linear data             ][ .. ]
                            ^
                            network_header (may be before data)

                         skb_headlen() = tail - data
                         skb->data_len = bytes in frags + frag_list
                         skb->len      = skb_headlen() + data_len

frags[]:   [ page 0 ][ page 1 ] ...   (page fragments)
frag_list: [ sk_buff ] --> [ sk_buff ] (linked fragments)
```

Its network header may precede or follow `skb->data`.
The supplied adapter uses it for the 20-byte IPv4 prefix.

## Your task

Edit one file:

- `access/skb_access.c`

Implement `rk_skb_read_network()`.

On success it must:

- copy exactly `length` bytes starting `offset` bytes after the network header;
- set `available` to the logical byte count from that header to the end of the
  `sk_buff`;
- work with linear data, page fragments, `frag_list`, shared data, and cloned
  buffers;
- leave the `sk_buff` unchanged.

Return the exact failure class:

- `RK_SKB_ACCESS_INVALID_ARGUMENT` for a null input or output pointer;
- `RK_SKB_ACCESS_INVALID_GEOMETRY` when the buffer's length fields or signed
  network-header offset are inconsistent;
- `RK_SKB_ACCESS_TRUNCATED` when the requested network-relative range is not
  present;
- `RK_SKB_ACCESS_UNREADABLE` when the buffer marks fragments unreadable or the
  safe-copy helper cannot retrieve the requested bytes;
- `RK_SKB_ACCESS_OK` after both outputs have been committed.

The supplied API accepts reads from 1 through 64 bytes.
It returns `RK_SKB_ACCESS_INVALID_ARGUMENT` for a larger or empty request.
An offset larger than `S64_MAX` is also an invalid request because it cannot
enter the checked signed addition used by the kernel helper.
Return `RK_SKB_ACCESS_TRUNCATED` if the requested offset or range exceeds the
logical network-relative bytes.
The IPv4 adapter requests exactly the first 20 bytes.

Apply checks in this order:

1. Invalid arguments: NULL pointers, empty or oversized request, offset >
   `S64_MAX`.
2. Base geometry: `skb_network_offset()` as signed value.
   Reject `INT_MIN`.
   For negative offset, prove it fits in `skb_headroom()`.
   For nonnegative, prove it does not exceed `skb->len`.
3. Unreadable fragments: `data_len > len` or lengths beyond signed helper range.
4. Truncated range: requested offset or range exceeds available network bytes.
5. Signed arithmetic fit: offset + request fits the helper's `int` interface.
   For negative final offset, also keep `skb_headlen() - offset` within `int`.
6. Copy: `skb_header_pointer_careful()` through a fixed local array.

Change neither output on failure.

The caller keeps the `sk_buff` geometry and data stable for the call.
`bytes` and `available` are valid, non-overlapping outputs.
They do not overlap the `sk_buff` or its packet storage.

Do not read from `skb->data` directly.
Do not linearize, pull, allocate, or write to the packet.

Run:

```sh
./rklings check
```

The checker uses `W=1`, real linear and nonlinear buffers, protected integration
code, and the complete bounded IPv4 parser corpus.

## References

- [Linux v7.2 `skb_header_pointer_careful()`][1]
- [Linux v7.2 `skb_copy_bits()` across linear data, page fragments, and
  `frag_list`][2]
- [Linux v7.2 `skb_frags_readable()`][3]
- [Linux v7.2 `skb_network_offset()`][4]

[1]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/skbuff.h#L4311-L4341
[2]: https://github.com/torvalds/linux/blob/v7.2/net/core/skbuff.c#L3019-L3097
[3]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/skbuff.h#L1890-L1894
[4]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/skbuff.h#L3252-L3255
