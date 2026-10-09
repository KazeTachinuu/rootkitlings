# Read packet bytes safely from an `sk_buff`

An `sk_buff` may span a linear head, page fragments, and `frag_list`.
Its network header may precede or follow `skb->data`.

Build one bounded, network-relative reader.
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

Use `skb_network_offset()` as a signed value.
A negative offset is valid when an earlier layer pulled `skb->data` past a
stored network header.
Reject `INT_MIN` before negating the offset, and prove a negative offset fits in
`skb_headroom()`.
For a nonnegative offset, prove it does not exceed `skb->len` before subtracting
it.

Reject `data_len > len`, lengths that exceed the signed helper interface, and
unreadable fragments before claiming an available byte count.
After validating the network-relative range, add the requested offset with a
wide signed type and prove the result fits the helper's signed `int` interface.
For a negative final offset, also keep the helper's `skb_headlen() - offset`
calculation within signed `int`.
Use `skb_header_pointer_careful()` to obtain the bytes.
Copy through a fixed local array and change neither output on failure.

After invalid arguments and base geometry, an unreadable-fragment marker takes
precedence over a truncated requested range.
If an otherwise present range cannot fit the helper's signed arithmetic, return
invalid geometry.
If the safe-copy helper still cannot retrieve a range that the length fields
claim exists, return unreadable.

The caller keeps the `sk_buff` geometry and data stable for the call.
`bytes` and `available` are valid, non-overlapping outputs.
They do not overlap the `sk_buff` or its packet storage.

Do not read from `skb->data` directly.
Do not linearize, pull, allocate, or write to the packet.

Run:

```bash
./rootkitlings check
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
