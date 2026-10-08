# Authenticated DNS signal

A fixed callback IP is easy to block and becomes useless when the controller moves.

A domain lets the operator change the address behind it, but one fixed domain is still easy to block.

A domain generation algorithm gives the implant and operator the same changing rendezvous name without transmitting that name first.

DNS can also carry a small signal in its query labels.

This module combines both ideas in one local, inspectable packet.

## What is sent

Writing `ready-7` to `/proc/rk_dns_c2` sends one DNS TXT question to `127.0.0.1:5300`:

```text
ready-7.<HMAC tag>.<campaign label>.example
```

For day bucket `20000`:

```text
ready-7.0123456789abcdef.6f158b1adc32cad8.example
   |             |               |           |
 signal      authenticity    rendezvous    reserved
```

The tag shown above only illustrates the shape.

The module calculates the real first 16 hexadecimal HMAC characters from the signal.

HMAC proves that someone with the shared key created the label; it does not hide the plaintext signal.

The 64-bit truncated tag is intentionally compact and weaker than the full SHA-256 result.

The embedded laboratory key can also be recovered from the module, so this is a protocol lesson rather than secret storage.

## See the packet

Start a UDP receiver inside the VM:

```sh
nc -u -l -p 5300 > /tmp/query.bin &
```

Load a repeatable campaign bucket and send a signal:

```sh
insmod hello.ko bucket=20000
echo ready-7 > /proc/rk_dns_c2
cat /proc/rk_dns_c2
```

```text
bucket=20000
query=ready-7.<tag>.6f158b1adc32cad8.example
id=<random 16-bit value>
sent=<packet bytes>
```

Inspect the DNS header and encoded labels:

```sh
hexdump -C /tmp/query.bin
```

The destination and `.example` suffix keep the exercise on loopback and out of public DNS.

## Wire format

DNS does not place dots in the packet name.

Each label starts with one length byte, and a zero byte terminates the name:

```text
  12-byte header
        |
        v
+------------+----+--------+----+------+----+----------+----+---------+
| id + flags | 07 | ready-7| 10 | tag  | 10 | campaign | 07 | example |
+------------+----+--------+----+------+----+----------+----+----+----+
                                                               | 00 |
                                                               +----+
                                                                  |
                                                             TXT + IN
```

The complete question is:

```text
header | encoded QNAME | 00 | QTYPE=TXT | QCLASS=IN
```

The transaction ID is random for every packet.

The send succeeds only when `kernel_sendmsg()` reports the exact packet length.

## Campaign bucket

Both endpoints calculate the campaign label from the same SipHash key and day bucket:

```text
shared key + bucket 20000
             |
             v
       SipHash-2-4
             |
             v
     6f158b1adc32cad8
```

`bucket=0` selects the current Unix day.

An explicit bucket makes packet construction repeatable during testing.

## Files

```text
main.c      bounded proc transaction and live status
auth.c      HMAC tag and keyed campaign label
dns.c       label validation, DNS encoding and UDP send
dns_c2.h    protocol sizes and component contracts
```

## Your work

Complete `build_name()` in `dns.c`.

Calculate the HMAC tag with `auth_tag()` and the campaign label with `campaign_label()`.

Append these four labels in order:

```text
signal -> tag -> campaign -> example
```

Stop and return the first error from either authentication or `append_label()`.

Only after all labels succeed, format the same dotted name into `query`.

The DNS header, bounds checks, random transaction ID, exact send check, socket cleanup, and proc transaction are provided.

## Verify

```sh
./rootkitlings check C09
```

The check captures the real datagram, decodes every label, independently calculates the HMAC for a runtime signal, verifies the campaign label and TXT/IN question fields, and rejects malformed labels without changing status.

Changing domains, fixed suffixes, regular query cadence, unusual label entropy, and repeated failed lookups are all useful defensive signals.
