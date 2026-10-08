# 46 - Carry a message in ICMP

An ICMP echo request can carry payload bytes after its header.

This module accepts payload only from echo requests whose identifier is `0xdead`, stores the latest message, and exposes it through `/proc/rk_covert`.

## Observe it

On the host, boot the exercise VM:

```sh
./rootkitlings run L46
```

Inside the VM, send a message with the expected identifier:

```sh
insmod hello.ko
cat /proc/rk_covert
icmp_send 127.0.0.1 dead 'secret_orders'
cat /proc/rk_covert
```

```text
magic=0xdead
msg=secret_orders
```

A normal ping leaves the initial `msg=(none)` state unchanged.

## Payload boundary

```text
IPv4 header       ICMP echo header       payload
+------------+    +----------------+    +-------------+
| ihl, length|--->| type, id=0xdead|--->| message     |
+------------+    +----------------+    +-------------+
```

The payload length is the IPv4 total length minus the variable IPv4 header and fixed ICMP header.

Both lengths must be validated before subtraction.

An `skb` may be non-linear, so `skb_copy_bits()` copies the payload into a local candidate instead of using direct pointer arithmetic.

Reject an embedded NUL before appending the terminator so the byte length and the stored C string describe the same message.

Only a complete, valid candidate is published, so a failed copy or invalid payload leaves the previous message intact.

The hook writes from softirq context, where a mutex cannot be used.

A process reading procfs can be interrupted by that writer on the same CPU, so both sides use `spin_lock_irqsave()` to prevent an interrupt from waiting on a lock held by the interrupted reader.

The writer holds the lock only for the short `memcpy()` and `msg_len` update.

The proc reader takes the same lock to copy a consistent snapshot, then formats it after unlocking.

## Implement it

The scaffold already validates both headers, matches the echo type and identifier, calculates a bounded payload range, and snapshots published state for proc reads.

At the marked block, copy `payload_len` bytes into `candidate` with `skb_copy_bits()` before taking the lock.

Return without changing the published message if the copy fails or `memchr()` finds an embedded NUL.

Append `\0` to the validated candidate.

Take `msg_lock` with `spin_lock_irqsave()`, copy the complete candidate into `last_msg`, publish `msg_len`, and release the lock.

## Verify

```sh
./rootkitlings check L46
```

The verification sends ordinary ICMP traffic and a magic echo carrying a runtime message, then requires the live proc entry to contain only the triggered payload.

The identifier and payload travel in plaintext and remain visible to capture outside the guest.
