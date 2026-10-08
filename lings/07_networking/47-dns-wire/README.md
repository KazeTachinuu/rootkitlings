# 47 - Encode a DNS name

DNS does not transmit `alpha.dead.example` as one dotted string.

Each label starts with one length byte, and a zero byte terminates the complete name.

```text
alpha.dead.example

+---+-------+---+------+---+---------+---+
| 5 | alpha | 4 | dead | 7 | example | 0 |
+---+-------+---+------+---+---------+---+
 len  label  len  label  len   label   root
```

A label can contain at most 63 bytes and an uncompressed wire name can contain at most 255 bytes.

## Observe it

Write a dotted name and read the independently decoded result:

```sh
insmod hello.ko
echo alpha.dead.example > /proc/rk_dns
cat /proc/rk_dns
```

```text
name=alpha.dead.example
wire_len=20
```

An empty label, trailing dot, label longer than 63 bytes, or name that exceeds the destination buffer is rejected without replacing the previous state.

## Encoding loop

Start at the first label and find the next dot.

Reject a zero or oversized label before writing anything for it.

Check that the length byte, label bytes, and final root byte still fit.

Write the length, copy the label, and continue after the dot.

After the final label, append zero and return the total through `used`.

The supplied decoder does not accept compression pointers because the encoder produces only an uncompressed question name.

The proc write path encodes into a temporary candidate and publishes it under the mutex only after the complete name succeeds.

## Implement it

Complete `dns_encode()`.

Use `strchr()` to locate each dot, `strlen()` for the final label, and `memcpy()` only after all bounds for that label have passed.

Set `*used` only after appending the terminating zero byte.

The independent decoder, proc transaction, state preservation, and lifecycle are provided.

## Verify

```sh
./rootkitlings check L47
```

The verification requires an exact round trip for a runtime name.

It accepts a 63-byte label and a 255-byte wire name, rejects the corresponding 64-byte and 256-byte cases, and proves that empty labels, trailing dots, embedded NUL bytes, and failed writes leave the prior value intact.

DNS labels and query cadence remain observable even when an application assigns a covert meaning to the bytes.
